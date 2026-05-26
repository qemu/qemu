#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2026 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

from aspeed import AspeedTest
from qemu_test import Asset, exec_command_and_wait_for_pattern


class AST1040Machine(AspeedTest):

    ASSET_ZEPHYR_3_07 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/zephyr/releases/download/v00.03.07/ast1040-evb-demo.zip'),
         'b5189797c22c2d732ddc27670c1efdeba821a2747c9c7434f190791125baa121')

    def test_arm_ast1040_zephyros(self):
        self.set_machine('ast1040-evb')

        kernel_name = "zephyr.bin"
        kernel_file = self.archive_extract(
            self.ASSET_ZEPHYR_3_07, member=kernel_name)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file, '-nographic')
        self.vm.launch()
        self.wait_for_console_pattern("uart:~$")
        exec_command_and_wait_for_pattern(self, "help",
                                          "Available commands")

if __name__ == '__main__':
    AspeedTest.main()
