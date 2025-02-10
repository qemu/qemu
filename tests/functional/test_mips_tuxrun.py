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

class TuxRunMipsTest(TuxRunBaselineTest):

    ASSET_MIPS_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips32/vmlinux',
        'b6f97fc698ae8c96456ad8c996c7454228074df0d7520dedd0a15e2913700a19')
    ASSET_MIPS_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/mips32/rootfs.ext4.zst',
        '87055cf3cbde3fd134e5039e7b87feb03231d8c4b21ee712b8ba3308dfa72f50')

    def test_mips32(self):
        self.set_machine('malta')
        self.cpu="mips32r6-generic"
        self.root="sda"
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_MIPS_KERNEL,
                           rootfs_asset=self.ASSET_MIPS_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
