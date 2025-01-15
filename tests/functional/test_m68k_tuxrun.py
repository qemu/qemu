#!/usr/bin/env python3
#
# Functional test that boots known good tuxboot images the same way
# that tuxrun (www.tuxrun.org) does. This tool is used by things like
# the LKFT project to run regression tests on kernels.
#
# Copyright (c) 2024 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from qemu_test.tuxruntest import TuxRunBaselineTest

class TuxRunM68KTest(TuxRunBaselineTest):

    ASSET_M68K_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/m68k/vmlinux',
        '7754e1d5cec753ccf1dc6894729a7f54c1a4965631ebf56df8e4ce1163ad19d8')
    ASSET_M68K_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/m68k/rootfs.ext4.zst',
        '557962ffff265607912e82232cf21adbe0e4e5a88e1e1d411ce848c37f0213e9')

    def test_m68k(self):
        self.set_machine('virt')
        self.cpu="m68040"
        self.common_tuxrun(kernel_asset=self.ASSET_M68K_KERNEL,
                           rootfs_asset=self.ASSET_M68K_ROOTFS,
                           drive="virtio-blk-device")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
