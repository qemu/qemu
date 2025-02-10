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
        'https://storage.tuxboot.com/buildroot/20241119/i386/bzImage',
        '47fb44e38e34101eb0f71a2a01742b959d40ed5fd67cefb5608a39be11d3b74e')
    ASSET_I386_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/i386/rootfs.ext4.zst',
        'a1a3b3b4c9dccd6475b58db95c107b468b736b700f6620985a8ed050a73d51c8')

    def test_i386(self):
        self.set_machine('q35')
        self.cpu="coreduo"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_I386_KERNEL,
                           rootfs_asset=self.ASSET_I386_ROOTFS,
                           drive="virtio-blk-pci")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
