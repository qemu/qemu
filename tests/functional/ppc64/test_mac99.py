#!/usr/bin/env python3
#
# Functional test that boots a mac99 machine with a PPC970 CPU
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern

class Mac99Test(LinuxKernelTest):

    ASSET_BR2_MAC99_LINUX = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main'
         '/buildroot/qemu_ppc64_mac99-2023.11-8-gdcd9f0f6eb-20240105/vmlinux'),
        'd59307437e4365f2cced0bbd1b04949f7397b282ef349b7cafd894d74aadfbff')

    ASSET_BR2_MAC99_ROOTFS = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main'
         '/buildroot/qemu_ppc64_mac99-2023.11-8-gdcd9f0f6eb-20240105/rootfs.ext2'),
        'bbd5fd8af62f580bc4e585f326fe584e22856572633a8333178ea6d4ed4955a4')

    def test_ppc64_mac99_buildroot(self):
        self.set_machine('mac99')

        linux_path = self.ASSET_BR2_MAC99_LINUX.fetch()
        rootfs_path = self.ASSET_BR2_MAC99_ROOTFS.fetch()

        self.vm.set_console()

        # Note: We need '-nographic' to get a serial console
        self.vm.add_args('-kernel', linux_path,
                         '-append', 'root=/dev/sda',
                         '-drive', f'file={rootfs_path},format=raw',
                         '-snapshot', '-nographic')
        self.vm.launch()

        self.wait_for_console_pattern('>> OpenBIOS')
        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('/init as init process')
        self.wait_for_console_pattern('gem 0000:f0:0e.0 eth0: Link is up at 100 Mbps')
        self.wait_for_console_pattern('buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')
        exec_command_and_wait_for_pattern(self, 'poweroff', 'Power down')

if __name__ == '__main__':
    LinuxKernelTest.main()
