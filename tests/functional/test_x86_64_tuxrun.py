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

class TuxRunX86Test(TuxRunBaselineTest):

    ASSET_X86_64_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/x86_64/bzImage',
        '2bc7480a669ee9b6b82500a236aba0c54233debe98cb968268fa230f52f03461')
    ASSET_X86_64_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/x86_64/rootfs.ext4.zst',
        'b72ac729769b8f51c6dffb221113c9a063c774dbe1d66af30eb593c4e9999b4b')

    def test_x86_64(self):
        self.set_machine('q35')
        self.cpu="Nehalem"
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_X86_64_KERNEL,
                           rootfs_asset=self.ASSET_X86_64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
