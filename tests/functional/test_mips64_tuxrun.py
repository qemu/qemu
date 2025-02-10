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

class TuxRunMips64Test(TuxRunBaselineTest):

    ASSET_MIPS64_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips64/vmlinux',
        'fe2882d216898ba2c56b49ba59f46ad392f36871f7fe325373cd926848b9dbdc')
    ASSET_MIPS64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips64/rootfs.ext4.zst',
        'b8c98400216b6d4fb3b3ff05e9929aa015948b596cf0b82234813c84a4f7f4d5')

    def test_mips64(self):
        self.set_machine('malta')
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPS64_KERNEL,
                           rootfs_asset=self.ASSET_MIPS64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
