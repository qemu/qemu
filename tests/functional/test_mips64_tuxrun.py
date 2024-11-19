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
        'https://storage.tuxboot.com/20230331/mips64/vmlinux',
        '09010e51e4b8bcbbd2494786ffb48eca78f228e96e5c5438344b0eac4029dc61')
    ASSET_MIPS64_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/mips64/rootfs.ext4.zst',
        '69d91eeb04df3d8d172922c6993bb37d4deeb6496def75d8580f6f9de3e431da')

    def test_mips64(self):
        self.set_machine('malta')
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPS64_KERNEL,
                           rootfs_asset=self.ASSET_MIPS64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
