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
        'https://storage.tuxboot.com/buildroot/20241119/arm64/Image',
        'b74743c5e89e1cea0f73368d24ae0ae85c5204ff84be3b5e9610417417d2f235')
    ASSET_ARM64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64/rootfs.ext4.zst',
        'a1acaaae2068df4648d04ff75f532aaa8c5edcd6b936122b6f0db4848a07b465')

    def test_arm64(self):
        self.set_machine('virt')
        self.cpu='cortex-a57'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64_KERNEL,
                           rootfs_asset=self.ASSET_ARM64_ROOTFS)

    ASSET_ARM64BE_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64be/Image',
        'fd6af4f16689d17a2c24fe0053cc212edcdf77abdcaf301800b8d38fa9f6e109')
    ASSET_ARM64BE_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/arm64be/rootfs.ext4.zst',
        'f5e9371b62701aab8dead52592ca7488c8a9e255c9be8d7635c7f30f477c2c21')

    def test_arm64be(self):
        self.set_machine('virt')
        self.cpu='cortex-a57'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARM64BE_KERNEL,
                           rootfs_asset=self.ASSET_ARM64BE_ROOTFS)

if __name__ == '__main__':
    TuxRunBaselineTest.main()
