#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time
import tempfile
import subprocess

from qemu_test import Asset
from aspeed import AspeedTest
from qemu_test import exec_command_and_wait_for_pattern, skipIfMissingCommands


class AST2600Machine(AspeedTest):

    ASSET_BR2_202411_AST2600_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2600-evb/buildroot-2024.11/flash.img'),
        '4bb2f3dfdea31199b51d66b42f686dc5374c144a7346fdc650194a5578b73609')

    def test_arm_ast2600_evb_buildroot(self):
        self.set_machine('ast2600-evb')

        image_path = self.ASSET_BR2_202411_AST2600_FLASH.fetch()

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

    ASSET_BR2_202302_AST2600_TPM_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2600-evb/buildroot-2023.02-tpm/flash.img'),
        'a46009ae8a5403a0826d607215e731a8c68d27c14c41e55331706b8f9c7bd997')

    @skipIfMissingCommands('swtpm')
    def test_arm_ast2600_evb_buildroot_tpm(self):
        self.set_machine('ast2600-evb')

        image_path = self.ASSET_BR2_202302_AST2600_TPM_FLASH.fetch()

        tpmstate_dir = tempfile.TemporaryDirectory(prefix="qemu_")
        socket = os.path.join(tpmstate_dir.name, 'swtpm-socket')

        # We must put the TPM state dir in /tmp/, not the build dir,
        # because some distros use AppArmor to lock down swtpm and
        # restrict the set of locations it can access files in.
        subprocess.run(['swtpm', 'socket', '-d', '--tpm2',
                        '--tpmstate', f'dir={tpmstate_dir.name}',
                        '--ctrl', f'type=unixio,path={socket}'])

        self.vm.add_args('-chardev', f'socket,id=chrtpm,path={socket}')
        self.vm.add_args('-tpmdev', 'emulator,id=tpm0,chardev=chrtpm')
        self.vm.add_args('-device',
                         'tpm-tis-i2c,tpmdev=tpm0,bus=aspeed.i2c.bus.12,address=0x2e')
        self.do_test_arm_aspeed_buildroot_start(image_path, '0xf00', 'Aspeed AST2600 EVB')

        exec_command_and_wait_for_pattern(self,
            'echo tpm_tis_i2c 0x2e > /sys/bus/i2c/devices/i2c-12/new_device',
            'tpm_tis_i2c 12-002e: 2.0 TPM (device-id 0x1, rev-id 1)')
        exec_command_and_wait_for_pattern(self,
            'cat /sys/class/tpm/tpm0/pcr-sha256/0',
            'B804724EA13F52A9072BA87FE8FDCC497DFC9DF9AA15B9088694639C431688E0')

        self.do_test_arm_aspeed_buildroot_poweroff()

    ASSET_SDK_V806_AST2600_A2 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v08.06/ast2600-a2-obmc.tar.gz',
        '9083506135f622d5e7351fcf7d4e1c7125cee5ba16141220c0ba88931f3681a4')

    def test_arm_ast2600_evb_sdk(self):
        self.set_machine('ast2600-evb')

        self.archive_extract(self.ASSET_SDK_V806_AST2600_A2)

        self.vm.add_args('-device',
            'tmp105,bus=aspeed.i2c.bus.5,address=0x4d,id=tmp-test')
        self.vm.add_args('-device',
            'ds1338,bus=aspeed.i2c.bus.5,address=0x32')
        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-a2", "image-bmc"))

        self.wait_for_console_pattern('ast2600-a2 login:')

        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc', 'root@ast2600-a2:~#')

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

if __name__ == '__main__':
    AspeedTest.main()
