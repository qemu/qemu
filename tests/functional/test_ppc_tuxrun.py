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

class TuxRunPPC32Test(TuxRunBaselineTest):

    ASSET_PPC32_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/ppc32/uImage',
        '1a68f74b860fda022fb12e03c5efece8c2b8b590d96cca37a8481a3ae0b3f81f')
    ASSET_PPC32_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/ppc32/rootfs.ext4.zst',
        '8885b9d999cc24d679542a02e9b6aaf48f718f2050ece6b8347074b6ee41dd09')

    def test_ppc32(self):
        self.set_machine('ppce500')
        self.cpu='e500mc'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_PPC32_KERNEL,
                           rootfs_asset=self.ASSET_PPC32_ROOTFS,
                           drive="virtio-blk-pci")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
