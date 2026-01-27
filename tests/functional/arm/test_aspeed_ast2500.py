#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, exec_command_and_wait_for_pattern
from aspeed import AspeedTest


class AST2500Machine(AspeedTest):

    ASSET_BR2_202511_AST2500_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2500-evb/buildroot-2025.11/flash.img'),
        '31e5a8e280b982fb0e7c07eb71c94851002f99ac604dfe620e71a5d47cc87e78')

    def test_arm_ast2500_evb_buildroot(self):
        self.set_machine('ast2500-evb')

        image_path = self.ASSET_BR2_202511_AST2500_FLASH.fetch()

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test')
        self.do_test_arm_aspeed_buildroot_start(image_path, '0x0',
                                                'ast2500-evb login:')

        exec_command_and_wait_for_pattern(self,
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device lm75 at 0x4d')
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000)
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '18000')

        self.do_test_arm_aspeed_buildroot_poweroff()

    ASSET_SDK_V1000_AST2500 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v10.00/ast2500-default-obmc.tar.gz',
        '7d71a3f71d5f4d9f3451f59a73bf9baf8fd9f6a24107eb504a3216151a8b2b5b')

    def test_arm_ast2500_evb_sdk(self):
        self.set_machine('ast2500-evb')

        self.archive_extract(self.ASSET_SDK_V1000_AST2500)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2500-default", "image-bmc"))

        self.wait_for_console_pattern('ast2500-default login:')


if __name__ == '__main__':
    AspeedTest.main()
