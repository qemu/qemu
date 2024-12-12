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

from qemu_test import Asset, exec_command_and_wait_for_pattern
from qemu_test.tuxruntest import TuxRunBaselineTest

class TuxRunSh4Test(TuxRunBaselineTest):

    ASSET_SH4_KERNEL = Asset(
        'https://storage.tuxboot.com/20230331/sh4/zImage',
        '29d9b2aba604a0f53a5dc3b5d0f2b8e35d497de1129f8ee5139eb6fdf0db692f')
    ASSET_SH4_ROOTFS = Asset(
        'https://storage.tuxboot.com/20230331/sh4/rootfs.ext4.zst',
        '3592a7a3d5a641e8b9821449e77bc43c9904a56c30d45da0694349cfd86743fd')

    def test_sh4(self):
        self.set_machine('r2d')
        self.cpu='sh7785'
        self.root='sda'
        self.console='ttySC1'

        # The test is currently too unstable to do much in userspace
        # so we skip common_tuxrun and do a minimal boot and shutdown.
        (kernel, disk, dtb) = self.fetch_tuxrun_assets(self.ASSET_SH4_KERNEL,
                                                       self.ASSET_SH4_ROOTFS)

        # the console comes on the second serial port
        self.prepare_run(kernel, disk,
                         "driver=ide-hd,bus=ide.0,unit=0",
                         console_index=1)
        self.vm.launch()

        self.wait_for_console_pattern("tuxtest login:")
        exec_command_and_wait_for_pattern(self, 'root', 'root@tuxtest:~#')
        exec_command_and_wait_for_pattern(self, 'halt',
                                          "reboot: System halted")

if __name__ == '__main__':
    TuxRunBaselineTest.main()
