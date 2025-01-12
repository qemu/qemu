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

class TuxRunX86Test(TuxRunBaselineTest):

    ASSET_X86_64_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/x86_64/bzImage',
        'f57bfc6553bcd6e0a54aab86095bf642b33b5571d14e3af1731b18c87ed5aef8')
    ASSET_X86_64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/x86_64/rootfs.ext4.zst',
        '4b8b2a99117519c5290e1202cb36eb6c7aaba92b357b5160f5970cf5fb78a751')

    def test_x86_64(self):
        self.set_machine('q35')
        self.cpu="Nehalem"
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_X86_64_KERNEL,
                           rootfs_asset=self.ASSET_X86_64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
