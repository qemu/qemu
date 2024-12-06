#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time
import subprocess
import tempfile

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import interrupt_interactive_console_until_pattern
from qemu_test import has_cmd
from qemu_test.utils import archive_extract
from zipfile import ZipFile
from unittest import skipUnless

class AST2x00MachineMMC(LinuxKernelTest):

    ASSET_RAINIER_EMMC = Asset(
        ('https://fileserver.linaro.org/s/B6pJTwWEkzSDi36/download/'
         'mmc-p10bmc-20240617.qcow2'),
        'd523fb478d2b84d5adc5658d08502bc64b1486955683814f89c6137518acd90b')

    def test_arm_aspeed_emmc_boot(self):
        self.set_machine('rainier-bmc')
        self.require_netdev('user')

        image_path = self.ASSET_RAINIER_EMMC.fetch()

        self.vm.set_console()
        self.vm.add_args('-drive',
                         'file=' + image_path + ',if=sd,id=sd2,index=2',
                         '-net', 'nic', '-net', 'user', '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot SPL 2019.04')
        self.wait_for_console_pattern('Trying to boot from MMC1')
        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('eMMC 2nd Boot')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Booting Linux on physical CPU 0xf00')
        self.wait_for_console_pattern('mmcblk0: p1 p2 p3 p4 p5 p6 p7')
        self.wait_for_console_pattern('IBM eBMC (OpenBMC for IBM Enterprise')

if __name__ == '__main__':
    LinuxKernelTest.main()
