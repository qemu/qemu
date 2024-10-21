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

class TuxRunI386Test(TuxRunBaselineTest):

    ASSET_I386_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/i386/bzImage',
        'a3e5b32a354729e65910f5a1ffcda7c14a6c12a55e8213fb86e277f1b76ed956')
    ASSET_I386_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/i386/rootfs.ext4.zst',
        'f15e66b2bf673a210ec2a4b2e744a80530b36289e04f5388aab812b97f69754a')

    def test_i386(self):
        self.set_machine('q35')
        self.cpu="coreduo"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_I386_KERNEL,
                           rootfs_asset=self.ASSET_I386_ROOTFS,
                           drive="virtio-blk-pci")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
