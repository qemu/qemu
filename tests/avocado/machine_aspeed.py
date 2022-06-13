# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado.utils import archive


class AST1030Machine(QemuSystemTest):
    """Boots the zephyr os and checks that the console is operational"""

    timeout = 10

    def test_ast1030_zephyros(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast1030-evb
        """
        tar_url = ('https://github.com/AspeedTech-BMC'
                   '/zephyr/releases/download/v00.01.04/ast1030-evb-demo.zip')
        tar_hash = '4c6a8ce3a8ba76ef1a65dae419ae3409343c4b20'
        tar_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(tar_path, self.workdir)
        kernel_file = self.workdir + "/ast1030-evb-demo/zephyr.elf"
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file,
                         '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self, "Booting Zephyr OS")
        exec_command_and_wait_for_pattern(self, "help",
                                          "Available commands")

class AST2x00Machine(QemuSystemTest):

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def do_test_arm_aspeed(self, image):
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic')
        self.vm.launch()

        self.wait_for_console_pattern("U-Boot 2016.07")
        self.wait_for_console_pattern("## Loading kernel from FIT Image at 20080000")
        self.wait_for_console_pattern("Starting kernel ...")
        self.wait_for_console_pattern("Booting Linux on physical CPU 0x0")
        wait_for_console_pattern(self,
                "aspeed-smc 1e620000.spi: read control register: 203b0641")
        self.wait_for_console_pattern("ftgmac100 1e660000.ethernet eth0: irq ")
        self.wait_for_console_pattern("systemd[1]: Set hostname to")

    def test_arm_ast2400_palmetto_openbmc_v2_9_0(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:palmetto-bmc
        """

        image_url = ('https://github.com/openbmc/openbmc/releases/download/2.9.0/'
                     'obmc-phosphor-image-palmetto.static.mtd')
        image_hash = ('3e13bbbc28e424865dc42f35ad672b10f2e82cdb11846bb28fa625b48beafd0d')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        self.do_test_arm_aspeed(image_path)

    def test_arm_ast2500_romulus_openbmc_v2_9_0(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:romulus-bmc
        """

        image_url = ('https://github.com/openbmc/openbmc/releases/download/2.9.0/'
                     'obmc-phosphor-image-romulus.static.mtd')
        image_hash = ('820341076803f1955bc31e647a512c79f9add4f5233d0697678bab4604c7bb25')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        self.do_test_arm_aspeed(image_path)

    def do_test_arm_aspeed_buidroot_start(self, image, cpu_id):
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2019.04')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')
        self.wait_for_console_pattern('Booting Linux on physical CPU ' + cpu_id)
        self.wait_for_console_pattern('lease of 10.0.2.15')
        self.wait_for_console_pattern('Aspeed EVB')
        exec_command(self, 'root')
        time.sleep(0.1)

    def do_test_arm_aspeed_buidroot_poweroff(self):
        exec_command_and_wait_for_pattern(self, 'poweroff',
                                          'reboot: System halted');

    def test_arm_ast2500_evb_builroot(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2500-evb
        """

        image_url = ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
                     'images/ast2500-evb/buildroot-2022.05/flash.img')
        image_hash = ('549db6e9d8cdaf4367af21c36385a68bb465779c18b5e37094fc7343decccd3f')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test');
        self.do_test_arm_aspeed_buidroot_start(image_path, '0x0')

        exec_command_and_wait_for_pattern(self,
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device lm75 at 0x4d');
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '0')
        self.vm.command('qom-set', path='/machine/peripheral/tmp-test',
                        property='temperature', value=18000);
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon1/temp1_input', '18000')

        self.do_test_arm_aspeed_buidroot_poweroff()

    def test_arm_ast2600_evb_builroot(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2600-evb
        """

        image_url = ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
                     'images/ast2600-evb/buildroot-2022.05/flash.img')
        image_hash = ('6cc9e7d128fd4fa1fd01c883af67593cae8072c3239a0b8b6ace857f3538a92d')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        self.do_test_arm_aspeed_buidroot_start(image_path, '0xf00')
        self.do_test_arm_aspeed_buidroot_poweroff()
