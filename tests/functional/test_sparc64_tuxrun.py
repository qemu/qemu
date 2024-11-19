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

class TuxRunSparc64Test(TuxRunBaselineTest):

    ASSET_SPARC64_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/sparc64/vmlinux',
        'e34313e4325ff21deaa3d38a502aa09a373ef62b9bd4d7f8f29388b688225c55')
    ASSET_SPARC64_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/sparc64/rootfs.ext4.zst',
        'ad2f1dc436ab51583543d25d2c210cab478645d47078d30d129a66ab0e281d76')

    def test_sparc64(self):
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_SPARC64_KERNEL,
                           rootfs_asset=self.ASSET_SPARC64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
