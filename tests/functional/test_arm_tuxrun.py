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
        'https://storage.tuxboot.com/20230331/armv5/zImage',
        'c95af2f27647c12265d75e9df44c22ff5228c59855f54aaa70f41ec2842e3a4d')
    ASSET_ARMV5_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/armv5/rootfs.ext4.zst',
        '17177afa74e7294da0642861f08c88ca3c836764299a54bf6d1ce276cb9712a5')
    ASSET_ARMV5_DTB = Asset(
        'https://storage.tuxboot.com/20230331/armv5/versatile-pb.dtb',
        '0bc0c0b0858cefd3c32b385c0d66d97142ded29472a496f4f490e42fc7615b25')

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
        'https://storage.tuxboot.com/20230331/armv7/zImage',
        '4c7a22e9f15875bec06bd2a29d822496571eb297d4f22694099ffcdb19077572')
    ASSET_ARMV7_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/armv7/rootfs.ext4.zst',
        'ab1fbbeaddda1ffdd45c9405a28cd5370c20f23a7cbc809cc90dc9f243a8eb5a')

    def test_armv7(self):
        self.set_machine('virt')
        self.cpu='cortex-a15'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARMV7_KERNEL,
                           rootfs_asset=self.ASSET_ARMV7_ROOTFS)

    ASSET_ARMV7BE_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/armv7be/zImage',
        '7facc62082b57af12015b08f7fdbaf2f123ba07a478367853ae12b219afc9f2f')
    ASSET_ARMV7BE_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/armv7be/rootfs.ext4.zst',
        '42ed46dd2d59986206c5b1f6cf35eab58fe3fd20c96b41aaa16b32f3f90a9835')

    def test_armv7be(self):
        self.set_machine('virt')
        self.cpu='cortex-a15'
        self.console='ttyAMA0'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_ARMV7BE_KERNEL,
                           rootfs_asset=self.ASSET_ARMV7BE_ROOTFS)

if __name__ == '__main__':
    TuxRunBaselineTest.main()
