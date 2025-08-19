#!/usr/bin/env python3
#
# Functional test that boots known good tuxboot images the same way
# that tuxrun (www.tuxrun.org) does. This tool is used by things like
# the LKFT project to run regression tests on kernels.
#
# Copyright (c) 2023 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from qemu_test.tuxruntest import TuxRunBaselineTest

class TuxRunRiscV64Test(TuxRunBaselineTest):

    ASSET_RISCV64_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/riscv64/Image',
        '2bd8132a3bf21570290042324fff48c987f42f2a00c08de979f43f0662ebadba')
    ASSET_RISCV64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/riscv64/rootfs.ext4.zst',
        'aa4736a9872651dfc0d95e709465eedf1134fd19d42b8cb305bfd776f9801004')

    ASSET_RISCV32_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/riscv32/Image',
        '872bc8f8e0d4661825d5f47f7bec64988e9d0a8bd5db8917d57e16f66d83b329')
    ASSET_RISCV32_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/riscv32/rootfs.ext4.zst',
        '511ad34e63222db08d6c1da16fad224970de36517a784110956ba6a24a0ee5f6')

    def test_riscv64(self):
        self.set_machine('virt')
        self.common_tuxrun(kernel_asset=self.ASSET_RISCV64_KERNEL,
                           rootfs_asset=self.ASSET_RISCV64_ROOTFS)

    def test_riscv64_maxcpu(self):
        self.set_machine('virt')
        self.cpu='max'
        self.common_tuxrun(kernel_asset=self.ASSET_RISCV64_KERNEL,
                           rootfs_asset=self.ASSET_RISCV64_ROOTFS)

    def test_riscv64_rv32(self):
        self.set_machine('virt')
        self.cpu='rv32'
        self.common_tuxrun(kernel_asset=self.ASSET_RISCV32_KERNEL,
                           rootfs_asset=self.ASSET_RISCV32_ROOTFS)

if __name__ == '__main__':
    TuxRunBaselineTest.main()
