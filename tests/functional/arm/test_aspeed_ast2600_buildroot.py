#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time
import tempfile
import subprocess

from aspeed import AspeedTest
from qemu_test import Asset
from qemu_test import exec_command_and_wait_for_pattern


class AST2600Machine(AspeedTest):

    ASSET_BR2_202511_AST2600_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2600-evb/buildroot-2025.11/flash.img'),
        'c64a0755501393d570ca318e326e1e9f8372edc5a6452cdccc3649bc9fd2c138')

    def test_arm_ast2600_evb_buildroot(self):
        self.set_machine('ast2600-evb')

        image_path = self.ASSET_BR2_202511_AST2600_FLASH.fetch()

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test')
        self.vm.add_args('-device',
                         'ds1338,bus=aspeed.i2c.bus.3,address=0x32')
        self.vm.add_args('-device',
                         'i2c-echo,bus=aspeed.i2c.bus.3,address=0x42')
        self.do_test_arm_aspeed_buildroot_start(image_path, '0xf00',
                                                'ast2600-evb login:')

        exec_command_and_wait_for_pattern(self,
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device lm75 at 0x4d')
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000)
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '18000')

        exec_command_and_wait_for_pattern(self,
             'echo ds1307 0x32 > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device ds1307 at 0x32')
        year = time.strftime("%Y")
        exec_command_and_wait_for_pattern(self, 'hwclock -f /dev/rtc1', year)

        exec_command_and_wait_for_pattern(self,
             'echo slave-24c02 0x1064 > /sys/bus/i2c/devices/i2c-3/new_device',
             'i2c i2c-3: new_device: Instantiated device slave-24c02 at 0x64')
        exec_command_and_wait_for_pattern(self,
             'i2cset -y 3 0x42 0x64 0x00 0xaa i', '#')
        exec_command_and_wait_for_pattern(self,
             'hexdump /sys/bus/i2c/devices/3-1064/slave-eeprom',
             '0000000 ffaa ffff ffff ffff ffff ffff ffff ffff')
        self.do_test_arm_aspeed_buildroot_poweroff()


if __name__ == '__main__':
    AspeedTest.main()
