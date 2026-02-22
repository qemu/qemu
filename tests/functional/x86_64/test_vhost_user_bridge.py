#!/usr/bin/env python3
#
# Copyright (c) 2025 Software Freedom Conservancy, Inc.
#
# Author: Yodel Eldar <yodel.eldar@yodel.dev>
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Test vhost-user-bridge (vubr) functionality:

    1) Run vhost-user-bridge on the host.
    2) Launch a guest VM:
        a) Instantiate a unix domain socket to the vubr-created path
        b) Instantiate a vhost-user backend on top of that socket
        c) Map a virtio-net-pci device to the vhost-user backend
        d) Instantiate a UDP socket backend
        e) Instantiate a user-mode net backend
            i) Forward an ephemeral port to port 8080 in-guest with hostfwd=
            ii) Expose a generated scratch file to the guest with tftp=
        f) Hub the UDP and user-mode backends.
    3) Invoke tftp in the guest to download exported scratch file from the host.
    4) Serve a file to the host via http server in the guest.
"""

import os
import shutil
import subprocess
from qemu_test import Asset, LinuxKernelTest, which
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import is_readable_executable_file
from qemu_test import wait_for_console_pattern
from qemu_test.ports import Ports

class VhostUserBridge(LinuxKernelTest):

    ASSET_KERNEL_INITRAMFS = Asset(
        "https://github.com/yodel/vhost-user-bridge-test/raw/refs/heads/main/bzImage",
        "8860d7aa59434f483542cdf25b42eacae0d4d4aa7ec923af9589d1ad4703d42b")

    HOST_UUID = "ba4c2e39-627f-487d-ae3b-93cc5d783eb8"
    HOST_UUID_HSUM = \
        "d2932e34bf6c17b33e7325140b691e27c191d9ac4dfa550f68c09506facb09b9"

    GUEST_UUID = "143d2b21-fdf0-4c5e-a9ef-f35ebbac8945"
    GUEST_UUID_HSUM = \
        "14b64203f5cf2afe520f8be0fdfe630aafc1e85d1301f55a0d1681e68881f3a2"

    def configure_vm(self, ud_socket_path, lport, rport, hostfwd_port, tftpdir):
        self.require_accelerator("kvm")
        self.require_netdev("vhost-user")
        self.require_netdev("socket")
        self.require_netdev("hubport")
        self.require_netdev("user")
        self.require_device("virtio-net-pci")
        self.set_machine("q35")
        self.vm.add_args(
            "-cpu",      "host",
            "-accel",    "kvm",
            "-append",   "printk.time=0 console=ttyS0",
            "-smp",      "2",
            "-m",        "128M",
            "-object",   "memory-backend-memfd,id=mem0,"
                         "size=128M,share=on,prealloc=on",
            "-numa",     "node,memdev=mem0",
            "-chardev", f"socket,id=char0,path={ud_socket_path}",
            "-netdev",   "vhost-user,id=vhost0,chardev=char0,vhostforce=on",
            "-device",   "virtio-net-pci,netdev=vhost0",
            "-netdev",  f"socket,id=udp0,udp=localhost:{lport},"
                        f"localaddr=localhost:{rport}",
            "-netdev",   "hubport,id=hub0,hubid=0,netdev=udp0",
            "-netdev",  f"user,id=user0,tftp={tftpdir},"
                        f"hostfwd=tcp:127.0.0.1:{hostfwd_port}-:8080",
            "-netdev",   "hubport,id=hub1,hubid=0,netdev=user0"
        )

    def assemble_vubr_args(self, vubr_path, ud_socket_path, lport, rport):
        vubr_args = []

        if (stdbuf_path := which("stdbuf")) is None:
            self.log.info("Could not find stdbuf: vhost-user-bridge "
                          "log lines may appear out of order")
        else:
            vubr_args += [stdbuf_path, "-o0", "-e0"]

        vubr_args += [vubr_path, "-u", f"{ud_socket_path}",
                      "-l", f"127.0.0.1:{lport}", "-r", f"127.0.0.1:{rport}"]

        return vubr_args

    def test_vhost_user_bridge(self):
        prompt = "~ # "
        host_uuid_filename = "vubr-test-uuid.txt"
        guest_uuid_path = "/tmp/uuid.txt"
        kernel_path = self.ASSET_KERNEL_INITRAMFS.fetch()

        vubr_path = self.build_file("contrib", "vhost-user-bridge",
                                    "vhost-user-bridge")
        if not is_readable_executable_file(vubr_path):
            self.skipTest("Could not find a readable and executable "
                          "vhost-user-bridge")

        vubr_log_path = self.log_file("vhost-user-bridge.log")
        self.log.info("For the vhost-user-bridge application log,"
                     f" see: {vubr_log_path}")

        sock_dir = self.socket_dir()
        ud_socket_path = os.path.join(sock_dir.name, "vubr-test.sock")

        tftpdir = self.scratch_file("tftp")
        shutil.rmtree(tftpdir, ignore_errors=True)
        os.mkdir(tftpdir)
        host_uuid_path = self.scratch_file("tftp", host_uuid_filename)
        with open(host_uuid_path, "w", encoding="utf-8") as host_uuid_file:
            host_uuid_file.write(self.HOST_UUID)

        with Ports() as ports:
            # pylint: disable=unbalanced-tuple-unpacking
            lport, rport, hostfwd_port = ports.find_free_ports(3)

            self.configure_vm(ud_socket_path, lport, rport, hostfwd_port,
                              tftpdir)

            vubr_args = self.assemble_vubr_args(vubr_path, ud_socket_path,
                                                lport, rport)

            with open(vubr_log_path, "w", encoding="utf-8") as vubr_log, \
                 subprocess.Popen(vubr_args, stdin=subprocess.DEVNULL,
                                  stdout=vubr_log,
                                  stderr=subprocess.STDOUT) as vubr_proc:
                self.launch_kernel(kernel_path, wait_for=prompt)

                exec_command_and_wait_for_pattern(self,
                    f"tftp -g -r {host_uuid_filename} 10.0.2.2 ; "
                    f"sha256sum {host_uuid_filename}", self.HOST_UUID_HSUM)
                wait_for_console_pattern(self, prompt)

                exec_command_and_wait_for_pattern(self,
                    f"echo -n '{self.GUEST_UUID}' > {guest_uuid_path}", prompt)
                self.check_http_download(guest_uuid_path, self.GUEST_UUID_HSUM)
                wait_for_console_pattern(self, prompt)

                self.vm.shutdown()
                vubr_proc.terminate()
                vubr_proc.wait()

if __name__ == '__main__':
    LinuxKernelTest.main()
