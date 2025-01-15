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
        'https://storage.tuxboot.com/buildroot/20241119/sparc64/vmlinux',
        'a04cfb2e70a264051d161fdd93aabf4b2a9472f2e435c14ed18c5848c5fed261')
    ASSET_SPARC64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/sparc64/rootfs.ext4.zst',
        '479c3dc104c82b68be55e2c0c5c38cd473d0b37ad4badccde4775bb88ce34611')

    def test_sparc64(self):
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_SPARC64_KERNEL,
                           rootfs_asset=self.ASSET_SPARC64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
