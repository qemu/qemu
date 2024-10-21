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

class TuxRunS390xTest(TuxRunBaselineTest):

    ASSET_S390X_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/s390/bzImage',
        '0414e98dd1c3dafff8496c9cd9c28a5f8d04553bb5ba37e906a812b48d442ef0')
    ASSET_S390X_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/s390/rootfs.ext4.zst',
        '88c37c32276677f873a25ab9ec6247895b8e3e6f8259134de2a616080b8ab3fc')

    def test_s390(self):
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_S390X_KERNEL,
                           rootfs_asset=self.ASSET_S390X_ROOTFS,
                           drive="virtio-blk-ccw",
                           haltmsg="Requesting system halt")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
