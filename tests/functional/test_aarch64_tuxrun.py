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

class TuxRunAarch64Test(TuxRunBaselineTest):

    ASSET_ARM64_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/arm64/Image',
        'ce95a7101a5fecebe0fe630deee6bd97b32ba41bc8754090e9ad8961ea8674c7')
    ASSET_ARM64_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/arm64/rootfs.ext4.zst',
        'bbd5ed4b9c7d3f4ca19ba71a323a843c6b585e880115df3b7765769dbd9dd061')

    def test_arm64(self):
        self.set_machine('virt')
        self.cpu='cortex-a57'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64_KERNEL,
                           rootfs_asset=self.ASSET_ARM64_ROOTFS)

    ASSET_ARM64BE_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/arm64be/Image',
        'e0df4425eb2cd9ea9a283e808037f805641c65d8fcecc8f6407d8f4f339561b4')
    ASSET_ARM64BE_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/arm64be/rootfs.ext4.zst',
        'e6ffd8813c8a335bc15728f2835f90539c84be7f8f5f691a8b01451b47fb4bd7')

    def test_arm64be(self):
        self.set_machine('virt')
        self.cpu='cortex-a57'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64BE_KERNEL,
                           rootfs_asset=self.ASSET_ARM64BE_ROOTFS)

if __name__ == '__main__':
    TuxRunBaselineTest.main()
