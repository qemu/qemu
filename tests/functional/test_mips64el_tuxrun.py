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

class TuxRunMips64ELTest(TuxRunBaselineTest):

    ASSET_MIPS64EL_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/mips64el/vmlinux',
        'd4e08965e2155c4cccce7c5f34d18fe34c636cda2f2c9844387d614950155266')
    ASSET_MIPS64EL_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/mips64el/rootfs.ext4.zst',
        'fba585368f5915b1498ed081863474b2d7ec4e97cdd46d21bdcb2f9698f83de4')

    def test_mips64el(self):
        self.set_machine('malta')
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPS64EL_KERNEL,
                           rootfs_asset=self.ASSET_MIPS64EL_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
