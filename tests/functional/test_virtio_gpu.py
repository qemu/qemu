#!/usr/bin/env python3
#
# virtio-gpu tests
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import is_readable_executable_file


import os
import socket
import subprocess


def pick_default_vug_bin(test):
    bld_dir_path = test.build_file("contrib", "vhost-user-gpu", "vhost-user-gpu")
    if is_readable_executable_file(bld_dir_path):
        return bld_dir_path


class VirtioGPUx86(QemuSystemTest):

    KERNEL_COMMAND_LINE = "printk.time=0 console=ttyS0 rdinit=/bin/bash"
    ASSET_KERNEL = Asset(
        ("https://archives.fedoraproject.org/pub/archive/fedora"
         "/linux/releases/33/Everything/x86_64/os/images"
         "/pxeboot/vmlinuz"),
        '2dc5fb5cfe9ac278fa45640f3602d9b7a08cc189ed63fd9b162b07073e4df397')
    ASSET_INITRD = Asset(
        ("https://archives.fedoraproject.org/pub/archive/fedora"
         "/linux/releases/33/Everything/x86_64/os/images"
         "/pxeboot/initrd.img"),
        'c49b97f893a5349e4883452178763e402bdc5caa8845b226a2d1329b5f356045')

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(
            self,
            success_message,
            failure_message="Kernel panic - not syncing",
            vm=vm,
        )

    def test_virtio_vga_virgl(self):
        # FIXME: should check presence of virtio, virgl etc
        self.require_accelerator('kvm')

        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()

        self.vm.set_console()
        self.vm.add_args("-cpu", "host")
        self.vm.add_args("-m", "2G")
        self.vm.add_args("-machine", "pc,accel=kvm")
        self.vm.add_args("-device", "virtio-vga-gl")
        self.vm.add_args("-display", "egl-headless")
        self.vm.add_args(
            "-kernel",
            kernel_path,
            "-initrd",
            initrd_path,
            "-append",
            self.KERNEL_COMMAND_LINE,
        )
        try:
            self.vm.launch()
        except:
            # TODO: probably fails because we are missing the VirGL features
            self.skipTest("VirGL not enabled?")

        self.wait_for_console_pattern("as init process")
        exec_command_and_wait_for_pattern(
            self, "/usr/sbin/modprobe virtio_gpu", "features: +virgl +edid"
        )

    def test_vhost_user_vga_virgl(self):
        # FIXME: should check presence of vhost-user-gpu, virgl, memfd etc
        self.require_accelerator('kvm')

        vug = pick_default_vug_bin(self)
        if not vug:
            self.skipTest("Could not find vhost-user-gpu")

        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()

        # Create socketpair to connect proxy and remote processes
        qemu_sock, vug_sock = socket.socketpair(
            socket.AF_UNIX, socket.SOCK_STREAM
        )
        os.set_inheritable(qemu_sock.fileno(), True)
        os.set_inheritable(vug_sock.fileno(), True)

        self._vug_log_path = self.log_file("vhost-user-gpu.log")
        self._vug_log_file = open(self._vug_log_path, "wb")
        self.log.info('Complete vhost-user-gpu.log file can be '
                      'found at %s', self._vug_log_path)

        vugp = subprocess.Popen(
            [vug, "--virgl", "--fd=%d" % vug_sock.fileno()],
            stdin=subprocess.DEVNULL,
            stdout=self._vug_log_file,
            stderr=subprocess.STDOUT,
            shell=False,
            close_fds=False,
        )

        self.vm.set_console()
        self.vm.add_args("-cpu", "host")
        self.vm.add_args("-m", "2G")
        self.vm.add_args("-object", "memory-backend-memfd,id=mem,size=2G")
        self.vm.add_args("-machine", "pc,memory-backend=mem,accel=kvm")
        self.vm.add_args("-chardev", "socket,id=vug,fd=%d" % qemu_sock.fileno())
        self.vm.add_args("-device", "vhost-user-vga,chardev=vug")
        self.vm.add_args("-display", "egl-headless")
        self.vm.add_args(
            "-kernel",
            kernel_path,
            "-initrd",
            initrd_path,
            "-append",
            self.KERNEL_COMMAND_LINE,
        )
        try:
            self.vm.launch()
        except:
            # TODO: probably fails because we are missing the VirGL features
            self.skipTest("VirGL not enabled?")
        self.wait_for_console_pattern("as init process")
        exec_command_and_wait_for_pattern(self, "/usr/sbin/modprobe virtio_gpu",
                                          "features: +virgl +edid")
        self.vm.shutdown()
        qemu_sock.close()
        vugp.terminate()
        vugp.wait()

if __name__ == '__main__':
    QemuSystemTest.main()
