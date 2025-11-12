#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2025 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

from aspeed import AspeedTest
from qemu_test import Asset, exec_command_and_wait_for_pattern


class AST1060Machine(AspeedTest):
    ASSET_ASPEED_AST1060_PROT_3_02 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/aspeed-zephyr-project/releases/download/v03.02'
         '/ast1060_prot_v03.02.tgz'),
         'dd5f1adc935316ddd1906506a02e15567bd7290657b52320f1a225564cc175bd')

    def test_arm_ast1060_prot_3_02(self):
        self.set_machine('ast1060-evb')

        kernel_name = "ast1060_prot/zephyr.bin"
        kernel_file = self.archive_extract(
            self.ASSET_ASPEED_AST1060_PROT_3_02, member=kernel_name)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file, '-nographic')
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")
        exec_command_and_wait_for_pattern(self, "help",
                                          "Available commands")

    def test_arm_ast1060_otp_blockdev_device(self):
        self.vm.set_machine("ast1060-evb")

        kernel_name = "ast1060_prot/zephyr.bin"
        kernel_file = self.archive_extract(self.ASSET_ASPEED_AST1060_PROT_3_02,
                                           member=kernel_name)
        otp_img = self.generate_otpmem_image()

        self.vm.set_console()
        self.vm.add_args(
            "-kernel", kernel_file,
            "-blockdev", f"driver=file,filename={otp_img},node-name=otp",
            "-global", "aspeed-otp.drive=otp",
        )
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")

if __name__ == '__main__':
    AspeedTest.main()
