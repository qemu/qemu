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

class TuxRunMipsELTest(TuxRunBaselineTest):

    ASSET_MIPSEL_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips32el/vmlinux',
        '660dd8c7a6ca7a32d37b4e6348865532ab0edb66802e8cc07869338444cf4929')
    ASSET_MIPSEL_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips32el/rootfs.ext4.zst',
        'c5d69542bcaed54a4f34671671eb4be5c608ee02671d4d0436544367816a73b1')

    def test_mips32el(self):
        self.set_machine('malta')
        self.cpu="mips32r6-generic"
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPSEL_KERNEL,
                           rootfs_asset=self.ASSET_MIPSEL_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
