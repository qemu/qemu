#!/usr/bin/env python3
#
# Test for multiprocess qemu
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


import os
import socket

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern
from qemu_test import exec_command, exec_command_and_wait_for_pattern

class Multiprocess(QemuSystemTest):

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    ASSET_KERNEL_X86 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')

    ASSET_INITRD_X86 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/x86_64/os/images/pxeboot/initrd.img'),
        '3b6cb5c91a14c42e2f61520f1689264d865e772a1f0069e660a800d31dd61fb9')

    ASSET_KERNEL_AARCH64 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/aarch64/os/images/pxeboot/vmlinuz'),
        '3ae07fcafbfc8e4abeb693035a74fe10698faae15e9ccd48882a9167800c1527')

    ASSET_INITRD_AARCH64 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/aarch64/os/images/pxeboot/initrd.img'),
        '9fd230cab10b1dafea41cf00150e6669d37051fad133bd618d2130284e16d526')

    def do_test(self, kernel_asset, initrd_asset,
                kernel_command_line, machine_type):
        """Main test method"""
        self.require_accelerator('kvm')
        self.require_device('x-pci-proxy-dev')

        # Create socketpair to connect proxy and remote processes
        proxy_sock, remote_sock = socket.socketpair(socket.AF_UNIX,
                                                    socket.SOCK_STREAM)
        os.set_inheritable(proxy_sock.fileno(), True)
        os.set_inheritable(remote_sock.fileno(), True)

        kernel_path = kernel_asset.fetch()
        initrd_path = initrd_asset.fetch()

        # Create remote process
        remote_vm = self.get_vm()
        remote_vm.add_args('-machine', 'x-remote')
        remote_vm.add_args('-nodefaults')
        remote_vm.add_args('-device', 'lsi53c895a,id=lsi1')
        remote_vm.add_args('-object', 'x-remote-object,id=robj1,'
                           'devid=lsi1,fd='+str(remote_sock.fileno()))
        remote_vm.launch()

        # Create proxy process
        self.vm.set_console()
        self.vm.add_args('-machine', machine_type)
        self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-cpu', 'host')
        self.vm.add_args('-object',
                         'memory-backend-memfd,id=sysmem-file,size=2G')
        self.vm.add_args('--numa', 'node,memdev=sysmem-file')
        self.vm.add_args('-m', '2048')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line)
        self.vm.add_args('-device',
                         'x-pci-proxy-dev,'
                         'id=lsi1,fd='+str(proxy_sock.fileno()))
        self.vm.launch()
        wait_for_console_pattern(self, 'as init process',
                                 'Kernel panic - not syncing')
        exec_command(self, 'mount -t sysfs sysfs /sys')
        exec_command_and_wait_for_pattern(self,
                                          'cat /sys/bus/pci/devices/*/uevent',
                                          'PCI_ID=1000:0012')

    def test_multiprocess(self):
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        if self.arch == 'x86_64':
            kernel_command_line += 'console=ttyS0 rdinit=/bin/bash'
            self.do_test(self.ASSET_KERNEL_X86, self.ASSET_INITRD_X86,
                         kernel_command_line, 'pc')
        elif self.arch == 'aarch64':
            kernel_command_line += 'rdinit=/bin/bash console=ttyAMA0'
            self.do_test(self.ASSET_KERNEL_AARCH64, self.ASSET_INITRD_AARCH64,
                         kernel_command_line, 'virt,gic-version=3')
        else:
            assert False

if __name__ == '__main__':
    QemuSystemTest.main()
