#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class SabreliteMachine(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://ftp.debian.org/debian/dists/bookworm/main/installer-armhf/'
         '20230607/images/netboot/vmlinuz'),
        '5851e9c61465a26db810796db671c4729e66e81c365b3ebb1a4b7f772737605b')

    ASSET_INITRD = Asset(
        ('https://ftp.debian.org/debian/dists/bookworm/main/installer-armhf/'
         '20230607/images/netboot/initrd.gz'),
        'fe33f836232a9107716d34dc56d36dea81bab039243a2a42ea6583a50c9b92ca')

    ASSET_DTB = Asset(
        ('https://ftp.debian.org/debian/dists/bookworm/main/installer-armhf/'
         '20230607/images/device-tree/imx6q-sabrelite.dtb'),
        '3ad637c8ea55ebd058a67f686e050b82ccb0d1327bc283623c133c48ad87db44')

    def setUp(self):
        super().setUp()

        self.kernel_path = self.ASSET_KERNEL.fetch()
        self.initrd_path = self.ASSET_INITRD.fetch()
        self.dtb_path = self.ASSET_DTB.fetch()

    def test_console(self):
        self.require_accelerator("tcg")
        self.set_machine('sabrelite')
        self.vm.set_console(console_index=1)
        self.vm.add_args('-kernel', self.kernel_path,
                         '-initrd', self.initrd_path,
                         '-dtb', self.dtb_path)

        self.vm.launch()
        self.wait_for_console_pattern('Starting system log daemon:')

if __name__ == '__main__':
    LinuxKernelTest.main()
