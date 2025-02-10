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
        'https://storage.tuxboot.com/buildroot/20241119/ppc32/uImage',
        'aa5d81deabdb255a318c4bc5ffd6fdd2b5da1ef39f1955dcc35b671d258b68e9')
    ASSET_PPC32_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/ppc32/rootfs.ext4.zst',
        '67554f830269d6bf53b67c7dd206bcc821e463993d526b1644066fea8117019b')

    def test_ppc32(self):
        self.set_machine('ppce500')
        self.cpu='e500mc'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_PPC32_KERNEL,
                           rootfs_asset=self.ASSET_PPC32_ROOTFS,
                           drive="virtio-blk-pci")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
