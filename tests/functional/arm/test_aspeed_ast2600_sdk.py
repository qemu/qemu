#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from qemu_test import Asset
from aspeed import AspeedTest
from qemu_test import exec_command_and_wait_for_pattern


class AST2600Machine(AspeedTest):

    ASSET_SDK_V908_AST2600 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v09.08/ast2600-default-obmc.tar.gz',
        'a0414f14ad696550efe083c2156dbeda855c08cc9ae7f40fe1b41bf292295f82')

    def do_ast2600_pcie_test(self):
        exec_command_and_wait_for_pattern(self,
            'lspci -s 80:00.0',
            '80:00.0 Host bridge: '
            'ASPEED Technology, Inc. Device 2600')
        exec_command_and_wait_for_pattern(self,
            'lspci -s 80:08.0',
            '80:08.0 PCI bridge: '
            'ASPEED Technology, Inc. AST1150 PCI-to-PCI Bridge')
        exec_command_and_wait_for_pattern(self,
            'lspci -s 81:00.0',
            '81:00.0 Ethernet controller: '
            'Intel Corporation 82574L Gigabit Network Connection')
        exec_command_and_wait_for_pattern(self,
            'ip addr show dev eth4',
            'inet 10.0.2.15/24')

    def test_arm_ast2600_evb_sdk(self):
        self.set_machine('ast2600-evb')
        self.require_netdev('user')

        self.archive_extract(self.ASSET_SDK_V908_AST2600)

        self.vm.add_args('-device',
            'tmp105,bus=aspeed.i2c.bus.5,address=0x4d,id=tmp-test')
        self.vm.add_args('-device',
            'ds1338,bus=aspeed.i2c.bus.5,address=0x32')
        self.vm.add_args('-device', 'e1000e,netdev=net1,bus=pcie.0')
        self.vm.add_args('-netdev', 'user,id=net1')
        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-default", "image-bmc"))

        self.wait_for_console_pattern('ast2600-default login:')

        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc',
                                          'root@ast2600-default:~#')

        exec_command_and_wait_for_pattern(self,
            'echo lm75 0x4d > /sys/class/i2c-dev/i2c-5/device/new_device',
            'i2c i2c-5: new_device: Instantiated device lm75 at 0x4d')
        exec_command_and_wait_for_pattern(self,
             'cat /sys/class/hwmon/hwmon19/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000)
        exec_command_and_wait_for_pattern(self,
             'cat /sys/class/hwmon/hwmon19/temp1_input', '18000')

        exec_command_and_wait_for_pattern(self,
             'echo ds1307 0x32 > /sys/class/i2c-dev/i2c-5/device/new_device',
             'i2c i2c-5: new_device: Instantiated device ds1307 at 0x32')
        year = time.strftime("%Y")
        exec_command_and_wait_for_pattern(self,
             '/sbin/hwclock -f /dev/rtc1', year)
        self.do_ast2600_pcie_test()

    def test_arm_ast2600_otp_blockdev_device(self):
        self.vm.set_machine("ast2600-evb")

        image_path = self.archive_extract(self.ASSET_SDK_V908_AST2600)
        otp_img = self.generate_otpmem_image()

        self.vm.set_console()
        self.vm.add_args(
            "-blockdev", f"driver=file,filename={otp_img},node-name=otp",
            "-global", "aspeed-otp.drive=otp",
        )
        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern("ast2600-default login:")


if __name__ == '__main__':
    AspeedTest.main()
