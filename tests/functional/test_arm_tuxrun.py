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

class TuxRunArmTest(TuxRunBaselineTest):

    ASSET_ARMV5_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv5/zImage',
        '3931a3908dbcf0ec0fe292d035ffc4dfed95f797dedd4a59ccfcf7a46e6f92d4')
    ASSET_ARMV5_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv5/rootfs.ext4.zst',
        '60ff78b68c7021df378e4fc2d66d3b016484d1acc7e07fb8920c1d8e30f4571f')
    ASSET_ARMV5_DTB = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv5/versatile-pb.dtb',
        '50988e69ef3f3b08bfb9146e8fe414129990029e8dfbed444953b7e14809530a')

    def test_armv5(self):
        self.set_machine('versatilepb')
        self.cpu='arm926'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARMV5_KERNEL,
                           rootfs_asset=self.ASSET_ARMV5_ROOTFS,
                           dtb_asset=self.ASSET_ARMV5_DTB,
                           drive="virtio-blk-pci")

    ASSET_ARMV7_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv7/zImage',
        '1377bc3d90de5ce57ab17cd67429fe8b15c2e9964248c775c682b67e6299b991')
    ASSET_ARMV7_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv7/rootfs.ext4.zst',
        'ed2cbc69bd6b3fbd5cafb5ee961393c7cfbe726446f14301c67d6b1f28bfdb51')

    def test_armv7(self):
        self.set_machine('virt')
        self.cpu='cortex-a15'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARMV7_KERNEL,
                           rootfs_asset=self.ASSET_ARMV7_ROOTFS)

    ASSET_ARMV7BE_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv7be/zImage',
        'a244e6da99f1bbd254827ec7681bd4aac9eb1aa05aaebc6b15e5d289ebb683f3')
    ASSET_ARMV7BE_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/armv7be/rootfs.ext4.zst',
        'd4f9c57860a512163f30ecc69b2174d1a1bdeb853a43dc49a09cfcfe84e428ea')

    def test_armv7be(self):
        self.set_machine('virt')
        self.cpu='cortex-a15'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARMV7BE_KERNEL,
                           rootfs_asset=self.ASSET_ARMV7BE_ROOTFS)

if __name__ == '__main__':
    TuxRunBaselineTest.main()
