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
from qemu_test import exec_command
from qemu_test import has_cmd
from qemu_test.utils import archive_extract
from zipfile import ZipFile
from unittest import skipUnless

class AST1030Machine(LinuxKernelTest):

    ASSET_ZEPHYR_1_04 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/zephyr/releases/download/v00.01.04/ast1030-evb-demo.zip'),
        '4ac6210adcbc61294927918707c6762483fd844dde5e07f3ba834ad1f91434d3')

    def test_ast1030_zephyros_1_04(self):
        self.set_machine('ast1030-evb')

        zip_file = self.ASSET_ZEPHYR_1_04.fetch()

        kernel_name = "ast1030-evb-demo/zephyr.elf"
        with ZipFile(zip_file, 'r') as zf:
                     zf.extract(kernel_name, path=self.workdir)
        kernel_file = os.path.join(self.workdir, kernel_name)

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

        zip_file = self.ASSET_ZEPHYR_1_07.fetch()

        kernel_name = "ast1030-evb-demo/zephyr.bin"
        with ZipFile(zip_file, 'r') as zf:
                     zf.extract(kernel_name, path=self.workdir)
        kernel_file = os.path.join(self.workdir, kernel_name)

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

class AST2x00Machine(LinuxKernelTest):

    def do_test_arm_aspeed(self, machine, image):
        self.set_machine(machine)
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-snapshot')
        self.vm.launch()

        self.wait_for_console_pattern("U-Boot 2016.07")
        self.wait_for_console_pattern("## Loading kernel from FIT Image at 20080000")
        self.wait_for_console_pattern("Starting kernel ...")
        self.wait_for_console_pattern("Booting Linux on physical CPU 0x0")
        self.wait_for_console_pattern(
                "aspeed-smc 1e620000.spi: read control register: 203b0641")
        self.wait_for_console_pattern("ftgmac100 1e660000.ethernet eth0: irq ")
        self.wait_for_console_pattern("systemd[1]: Set hostname to")

    ASSET_PALMETTO_FLASH = Asset(
        ('https://github.com/openbmc/openbmc/releases/download/2.9.0/'
         'obmc-phosphor-image-palmetto.static.mtd'),
        '3e13bbbc28e424865dc42f35ad672b10f2e82cdb11846bb28fa625b48beafd0d');

    def test_arm_ast2400_palmetto_openbmc_v2_9_0(self):
        image_path = self.ASSET_PALMETTO_FLASH.fetch()

        self.do_test_arm_aspeed('palmetto-bmc', image_path)

    ASSET_ROMULUS_FLASH = Asset(
        ('https://github.com/openbmc/openbmc/releases/download/2.9.0/'
         'obmc-phosphor-image-romulus.static.mtd'),
        '820341076803f1955bc31e647a512c79f9add4f5233d0697678bab4604c7bb25')

    def test_arm_ast2500_romulus_openbmc_v2_9_0(self):
        image_path = self.ASSET_ROMULUS_FLASH.fetch()

        self.do_test_arm_aspeed('romulus-bmc', image_path)

    def do_test_arm_aspeed_buildroot_start(self, image, cpu_id, pattern='Aspeed EVB'):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw,read-only=true',
                         '-net', 'nic', '-net', 'user')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Booting Linux on physical CPU ' + cpu_id)
        self.wait_for_console_pattern('lease of 10.0.2.15')
        # the line before login:
        self.wait_for_console_pattern(pattern)
        time.sleep(0.1)
        exec_command(self, 'root')
        time.sleep(0.1)
        exec_command(self, "passw0rd")

    def do_test_arm_aspeed_buildroot_poweroff(self):
        exec_command_and_wait_for_pattern(self, 'poweroff',
                                          'reboot: System halted');

    ASSET_BR2_202311_AST2500_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2500-evb/buildroot-2023.11/flash.img'),
        'c23db6160cf77d0258397eb2051162c8473a56c441417c52a91ba217186e715f')

    def test_arm_ast2500_evb_buildroot(self):
        self.set_machine('ast2500-evb')

        image_path = self.ASSET_BR2_202311_AST2500_FLASH.fetch()

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test');
        self.do_test_arm_aspeed_buildroot_start(image_path, '0x0',
                                                'Aspeed AST2500 EVB')

        exec_command_and_wait_for_pattern(self,
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device lm75 at 0x4d');
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000);
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '18000')

        self.do_test_arm_aspeed_buildroot_poweroff()

    ASSET_BR2_202311_AST2600_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2600-evb/buildroot-2023.11/flash.img'),
        'b62808daef48b438d0728ee07662290490ecfa65987bb91294cafb1bb7ad1a68')

    def test_arm_ast2600_evb_buildroot(self):
        self.set_machine('ast2600-evb')

        image_path = self.ASSET_BR2_202311_AST2600_FLASH.fetch()

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test');
        self.vm.add_args('-device',
                         'ds1338,bus=aspeed.i2c.bus.3,address=0x32');
        self.vm.add_args('-device',
                         'i2c-echo,bus=aspeed.i2c.bus.3,address=0x42');
        self.do_test_arm_aspeed_buildroot_start(image_path, '0xf00', 'Aspeed AST2600 EVB')

        exec_command_and_wait_for_pattern(self,
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device lm75 at 0x4d');
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000);
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '18000')

        exec_command_and_wait_for_pattern(self,
             'echo ds1307 0x32 > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device ds1307 at 0x32');
        year = time.strftime("%Y")
        exec_command_and_wait_for_pattern(self, 'hwclock -f /dev/rtc1', year);

        exec_command_and_wait_for_pattern(self,
             'echo slave-24c02 0x1064 > /sys/bus/i2c/devices/i2c-3/new_device',
             'i2c i2c-3: new_device: Instantiated device slave-24c02 at 0x64');
        exec_command(self, 'i2cset -y 3 0x42 0x64 0x00 0xaa i');
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self,
             'hexdump /sys/bus/i2c/devices/3-1064/slave-eeprom',
             '0000000 ffaa ffff ffff ffff ffff ffff ffff ffff');
        self.do_test_arm_aspeed_buildroot_poweroff()

    ASSET_BR2_202302_AST2600_TPM_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2600-evb/buildroot-2023.02-tpm/flash.img'),
        'a46009ae8a5403a0826d607215e731a8c68d27c14c41e55331706b8f9c7bd997')

    @skipUnless(*has_cmd('swtpm'))
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
            'tpm_tis_i2c 12-002e: 2.0 TPM (device-id 0x1, rev-id 1)');
        exec_command_and_wait_for_pattern(self,
            'cat /sys/class/tpm/tpm0/pcr-sha256/0',
            'B804724EA13F52A9072BA87FE8FDCC497DFC9DF9AA15B9088694639C431688E0');

        self.do_test_arm_aspeed_buildroot_poweroff()

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
