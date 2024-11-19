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

class TuxRunMipsELTest(TuxRunBaselineTest):

    ASSET_MIPSEL_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/mips32el/vmlinux',
        '8573867c68a8443db8de6d08bb33fb291c189ca2ca671471d3973a3e712096a3')
    ASSET_MIPSEL_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/mips32el/rootfs.ext4.zst',
        'e799768e289fd69209c21f4dacffa11baea7543d5db101e8ce27e3bc2c41d90e')

    def test_mips32el(self):
        self.set_machine('malta')
        self.cpu="mips32r6-generic"
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPSEL_KERNEL,
                           rootfs_asset=self.ASSET_MIPSEL_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
