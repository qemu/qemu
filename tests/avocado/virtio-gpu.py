# virtio-gpu tests
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from avocado_qemu import BUILD_DIR
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import is_readable_executable_file

from qemu.utils import kvm_available

import os
import socket
import subprocess


def pick_default_vug_bin():
    relative_path = "./contrib/vhost-user-gpu/vhost-user-gpu"
    if is_readable_executable_file(relative_path):
        return relative_path

    bld_dir_path = os.path.join(BUILD_DIR, relative_path)
    if is_readable_executable_file(bld_dir_path):
        return bld_dir_path


class VirtioGPUx86(QemuSystemTest):
    """
    :avocado: tags=virtio-gpu
    :avocado: tags=arch:x86_64
    :avocado: tags=cpu:host
    """

    KERNEL_COMMAND_LINE = "printk.time=0 console=ttyS0 rdinit=/bin/bash"
    KERNEL_URL = (
        "https://archives.fedoraproject.org/pub/archive/fedora"
        "/linux/releases/33/Everything/x86_64/os/images"
        "/pxeboot/vmlinuz"
    )
    KERNEL_HASH = '1433cfe3f2ffaa44de4ecfb57ec25dc2399cdecf'
    INITRD_URL = (
        "https://archives.fedoraproject.org/pub/archive/fedora"
        "/linux/releases/33/Everything/x86_64/os/images"
        "/pxeboot/initrd.img"
    )
    INITRD_HASH = 'c828d68a027b53e5220536585efe03412332c2d9'

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(
            self,
            success_message,
            failure_message="Kernel panic - not syncing",
            vm=vm,
        )

    def test_virtio_vga_virgl(self):
        """
        :avocado: tags=device:virtio-vga-gl
        """
        # FIXME: should check presence of virtio, virgl etc
        self.require_accelerator('kvm')

        kernel_path = self.fetch_asset(self.KERNEL_URL, self.KERNEL_HASH)
        initrd_path = self.fetch_asset(self.INITRD_URL, self.INITRD_HASH)

        self.vm.set_console()
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
            self.cancel("VirGL not enabled?")

        self.wait_for_console_pattern("as init process")
        exec_command_and_wait_for_pattern(
            self, "/usr/sbin/modprobe virtio_gpu", ""
        )
        self.wait_for_console_pattern("features: +virgl +edid")

    def test_vhost_user_vga_virgl(self):
        """
        :avocado: tags=device:vhost-user-vga
        """
        # FIXME: should check presence of vhost-user-gpu, virgl, memfd etc
        self.require_accelerator('kvm')

        vug = pick_default_vug_bin()
        if not vug:
            self.cancel("Could not find vhost-user-gpu")

        kernel_path = self.fetch_asset(self.KERNEL_URL, self.KERNEL_HASH)
        initrd_path = self.fetch_asset(self.INITRD_URL, self.INITRD_HASH)

        # Create socketpair to connect proxy and remote processes
        qemu_sock, vug_sock = socket.socketpair(
            socket.AF_UNIX, socket.SOCK_STREAM
        )
        os.set_inheritable(qemu_sock.fileno(), True)
        os.set_inheritable(vug_sock.fileno(), True)

        self._vug_log_path = os.path.join(
            self.logdir, "vhost-user-gpu.log"
        )
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
            self.cancel("VirGL not enabled?")
        self.wait_for_console_pattern("as init process")
        exec_command_and_wait_for_pattern(self, "/usr/sbin/modprobe virtio_gpu",
                                          "features: +virgl +edid")
        self.vm.shutdown()
        qemu_sock.close()
        vugp.terminate()
        vugp.wait()
