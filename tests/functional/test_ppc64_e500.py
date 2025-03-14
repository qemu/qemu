#!/usr/bin/env python3
#
# Boot a Linux kernel on a e500 ppc64 machine and check the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class E500Test(LinuxKernelTest):

    ASSET_BR2_E5500_UIMAGE = Asset(
        'https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/buildroot/qemu_ppc64_e5500-2023.11-8-gdcd9f0f6eb-20240104/uImage',
        '2478187c455d6cca3984e9dfde9c635d824ea16236b85fd6b4809f744706deda')

    ASSET_BR2_E5500_ROOTFS = Asset(
        'https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main//buildroot/qemu_ppc64_e5500-2023.11-8-gdcd9f0f6eb-20240104/rootfs.ext2',
        '9035ef97237c84c7522baaff17d25cdfca4bb7a053d5e296e902919473423d76')

    def test_ppc64_e500_buildroot(self):
        self.set_machine('ppce500')
        self.require_netdev('user')
        self.cpu = 'e5500'

        uimage_path = self.ASSET_BR2_E5500_UIMAGE.fetch()
        rootfs_path = self.ASSET_BR2_E5500_ROOTFS.fetch()

        self.vm.set_console()
        self.vm.add_args('-kernel', uimage_path,
                         '-append', 'root=/dev/vda',
                         '-drive', f'file={rootfs_path},if=virtio,format=raw',
                         '-snapshot', '-no-shutdown')
        self.vm.launch()

        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('/init as init process')
        self.wait_for_console_pattern('lease of 10.0.2.15')
        self.wait_for_console_pattern('buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')
        exec_command_and_wait_for_pattern(self, 'poweroff', 'Power down')

if __name__ == '__main__':
    LinuxKernelTest.main()
