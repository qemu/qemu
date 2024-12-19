#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class AST1030Machine(LinuxKernelTest):

    ASSET_ZEPHYR_1_04 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/zephyr/releases/download/v00.01.04/ast1030-evb-demo.zip'),
        '4ac6210adcbc61294927918707c6762483fd844dde5e07f3ba834ad1f91434d3')

    def test_ast1030_zephyros_1_04(self):
        self.set_machine('ast1030-evb')

        kernel_name = "ast1030-evb-demo/zephyr.elf"
        kernel_file = self.archive_extract(
            self.ASSET_ZEPHYR_1_04, member=kernel_name)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file, '-nographic')
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")
        exec_command_and_wait_for_pattern(self, "help",
                                          "Available commands")

    ASSET_ZEPHYR_1_07 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/zephyr/releases/download/v00.01.07/ast1030-evb-demo.zip'),
        'ad52e27959746988afaed8429bf4e12ab988c05c4d07c9d90e13ec6f7be4574c')

    def test_ast1030_zephyros_1_07(self):
        self.set_machine('ast1030-evb')

        kernel_name = "ast1030-evb-demo/zephyr.bin"
        kernel_file = self.archive_extract(
            self.ASSET_ZEPHYR_1_07, member=kernel_name)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file, '-nographic')
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")
        for shell_cmd in [
                'kernel stacks',
                'otp info conf',
                'otp info scu',
                'hwinfo devid',
                'crypto aes256_cbc_vault',
                'random get',
                'jtag JTAG1 sw_xfer high TMS',
                'adc ADC0 resolution 12',
                'adc ADC0 read 42',
                'adc ADC1 read 69',
                'i2c scan I2C_0',
                'i3c attach I3C_0',
                'hash test',
                'kernel uptime',
                'kernel reboot warm',
                'kernel uptime',
                'kernel reboot cold',
                'kernel uptime',
        ]: exec_command_and_wait_for_pattern(self, shell_cmd, "uart:~$")


if __name__ == '__main__':
    LinuxKernelTest.main()
