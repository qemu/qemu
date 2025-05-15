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
        'https://storage.tuxboot.com/buildroot/20241119/s390/bzImage',
        'ee67e91db52a2aed104a7c72b2a08987c678f8179c029626789c35d6dd0fedf1')
    ASSET_S390X_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/s390/rootfs.ext4.zst',
        'bff7971fc2fef56372d98afe4557b82fd0a785a241e44c29b058e577ad1bbb44')

    def test_s390(self):
        self.set_machine('s390-ccw-virtio')
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_S390X_KERNEL,
                           rootfs_asset=self.ASSET_S390X_ROOTFS,
                           drive="virtio-blk-ccw",
                           haltmsg="Requesting system halt")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
