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

class TuxRunMipsTest(TuxRunBaselineTest):

    ASSET_MIPS_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/mips32/vmlinux',
        'bfd2172f8b17fb32970ca0c8c58f59c5a4ca38aa5855d920be3a69b5d16e52f0')
    ASSET_MIPS_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/mips32/rootfs.ext4.zst',
        'fc3da0b4c2f38d74c6d705123bb0f633c76ed953128f9d0859378c328a6d11a0')

    def test_mips32(self):
        self.set_machine('malta')
        self.cpu="mips32r6-generic"
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPS_KERNEL,
                           rootfs_asset=self.ASSET_MIPS_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
