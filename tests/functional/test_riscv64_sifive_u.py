#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Sifive U machine
# and checks the console
#
# Copyright (c) Linaro Ltd.
#
# Author:
#  Philippe Mathieu-Daudé
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import Asset, LinuxKernelTest
from qemu_test import skipIfMissingCommands


class SifiveU(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/riscv64/Image',
        '2bd8132a3bf21570290042324fff48c987f42f2a00c08de979f43f0662ebadba')
    ASSET_ROOTFS = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '9819da19e6eef291686fdd7b029ea00e764dc62f/rootfs/riscv64/'
         'rootfs.ext2.gz'),
        'b6ed95610310b7956f9bf20c4c9c0c05fea647900df441da9dfe767d24e8b28b')

    def test_riscv64_sifive_u_mmc_spi(self):
        self.set_machine('sifive_u')
        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.uncompress(self.ASSET_ROOTFS)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'root=/dev/mmcblk0 rootwait '
                               'earlycon=sbi console=ttySIF0 '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-drive', f'file={rootfs_path},if=sd,format=raw',
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        os.remove(rootfs_path)


if __name__ == '__main__':
    LinuxKernelTest.main()
