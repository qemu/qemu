#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys
import os

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test.utils import archive_extract

class AST2x00MachineSDK(QemuSystemTest):

    def do_test_aarch64_aspeed_sdk_start(self, image):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user', '-snapshot')

        self.vm.launch()

        wait_for_console_pattern(self, 'U-Boot 2023.10')
        wait_for_console_pattern(self, '## Loading kernel from FIT Image')
        wait_for_console_pattern(self, 'Starting kernel ...')

    ASSET_SDK_V902_AST2700 = Asset(
            'https://github.com/AspeedTech-BMC/openbmc/releases/download/v09.02/ast2700-default-obmc.tar.gz',
            'ac969c2602f4e6bdb69562ff466b89ae3fe1d86e1f6797bb7969d787f82116a7')

    def test_aarch64_ast2700_evb_sdk_v09_02(self):
        self.set_machine('ast2700-evb')

        image_path = self.ASSET_SDK_V902_AST2700.fetch()
        archive_extract(image_path, self.workdir)

        num_cpu = 4
        image_dir = self.workdir + '/ast2700-default/'
        uboot_size = os.path.getsize(image_dir + 'u-boot-nodtb.bin')
        uboot_dtb_load_addr = hex(0x400000000 + uboot_size)

        load_images_list = [
            {
                'addr': '0x400000000',
                'file': image_dir + 'u-boot-nodtb.bin'
            },
            {
                'addr': str(uboot_dtb_load_addr),
                'file': image_dir + 'u-boot.dtb'
            },
            {
                'addr': '0x430000000',
                'file': image_dir + 'bl31.bin'
            },
            {
                'addr': '0x430080000',
                'file': image_dir + 'optee/tee-raw.bin'
            }
        ]

        for load_image in load_images_list:
            addr = load_image['addr']
            file = load_image['file']
            self.vm.add_args('-device',
                             f'loader,force-raw=on,addr={addr},file={file}')

        for i in range(num_cpu):
            self.vm.add_args('-device',
                             f'loader,addr=0x430000000,cpu-num={i}')

        self.vm.add_args('-smp', str(num_cpu))
        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.1,address=0x4d,id=tmp-test')
        self.do_test_aarch64_aspeed_sdk_start(image_dir + 'image-bmc')

        wait_for_console_pattern(self, 'ast2700-default login:')

        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self,
            '0penBmc', 'root@ast2700-default:~#')

        exec_command_and_wait_for_pattern(self,
            'echo lm75 0x4d > /sys/class/i2c-dev/i2c-1/device/new_device ',
            'i2c i2c-1: new_device: Instantiated device lm75 at 0x4d');
        exec_command_and_wait_for_pattern(self,
            'cat /sys/class/hwmon/hwmon20/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000)
        exec_command_and_wait_for_pattern(self,
            'cat /sys/class/hwmon/hwmon20/temp1_input', '18000')


if __name__ == '__main__':
    QemuSystemTest.main()
