#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Milk-V Duo machine
#
# Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import skipIfMissingCommands, wait_for_console_pattern

class MilkvDuoMachine(LinuxKernelTest):

    timeout = 120

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.11.9/riscv64/Image',
        '174f8bb87f08961e54fa3fcd954a8e31f4645f6d6af4dd43983d5e9841490fb0'
    )
    ASSET_DTBS = Asset(
        'https://storage.tuxboot.com/kernels/6.11.9/riscv64/dtbs.tar.xz',
        '97e5e65a7e6c68303e53486af120c0846ee8df572e9c15f0c4dccb831d0cef4b'
    )
    ASSET_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/riscv64/rootfs.ext4.zst',
        'aa4736a9872651dfc0d95e709465eedf1134fd19d42b8cb305bfd776f9801004'
    )

    @skipIfMissingCommands('zstd')
    def test_riscv64_milkv_duo(self):
        self.set_machine('milkv-duo')

        kernel_path = self.ASSET_KERNEL.fetch()
        dtb_path = self.archive_extract(
            self.ASSET_DTBS, member='dtbs/sophgo/cv1800b-milkv-duo.dtb')
        rootfs_path = self.uncompress(self.ASSET_ROOTFS)

        self.vm.set_console()
        self.vm.add_args('-bios', 'default',
                         '-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', f'file={rootfs_path},format=raw,id=sd-card,snapshot=on',
                         '-device', 'sd-card,drive=sd-card',
                         '-append', 'console=ttyS0,115200 root=/dev/mmcblk0 rootwait earlycon')

        self.vm.launch()

        wait_for_console_pattern(self, 'Machine model: Milk-V Duo')
        wait_for_console_pattern(self, 'Welcome to TuxTest')
        wait_for_console_pattern(self, 'tuxtest login:')

if __name__ == '__main__':
    LinuxKernelTest.main()
