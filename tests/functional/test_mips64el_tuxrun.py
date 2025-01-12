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

class TuxRunMips64ELTest(TuxRunBaselineTest):

    ASSET_MIPS64EL_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips64el/vmlinux',
        '0d2829a96f005229839c4cd586d4d8a136ea4b488d29821611c8e97f2266bfa9')
    ASSET_MIPS64EL_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips64el/rootfs.ext4.zst',
        '69c8b69a4f1582ce4c6f01a994968f5d73bffb2fc99cbeeeb26c8b5a28eaeb84')

    def test_mips64el(self):
        self.set_machine('malta')
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPS64EL_KERNEL,
                           rootfs_asset=self.ASSET_MIPS64EL_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
