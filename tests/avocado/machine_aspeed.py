# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time
import os
import tempfile
import subprocess

from avocado_qemu import LinuxSSHMixIn
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import has_cmd
from avocado.utils import archive
from avocado import skipUnless
from avocado import skipUnless


class AST1030Machine(QemuSystemTest):
    """Boots the zephyr os and checks that the console is operational"""

    timeout = 10

    def test_ast1030_zephyros_1_04(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast1030-evb
        :avocado: tags=os:zephyr
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

    def test_ast1030_zephyros_1_07(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast1030-evb
        :avocado: tags=os:zephyr
        """
        tar_url = ('https://github.com/AspeedTech-BMC'
                   '/zephyr/releases/download/v00.01.07/ast1030-evb-demo.zip')
        tar_hash = '40ac87eabdcd3b3454ce5aad11fedc72a33ecda2'
        tar_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(tar_path, self.workdir)
        kernel_file = self.workdir + "/ast1030-evb-demo/zephyr.bin"
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file,
                         '-nographic')
        self.vm.launch()
        wait_for_console_pattern(self, "Booting Zephyr OS")
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

class AST2x00Machine(QemuSystemTest):

    timeout = 90

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

    def do_test_arm_aspeed_buildroot_start(self, image, cpu_id, pattern='Aspeed EVB'):
        self.require_netdev('user')

        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
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

    def do_test_arm_aspeed_buildroot_poweroff(self):
        exec_command_and_wait_for_pattern(self, 'poweroff',
                                          'reboot: System halted');

    def test_arm_ast2500_evb_buildroot(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2500-evb
        """

        image_url = ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
                     'images/ast2500-evb/buildroot-2022.11-2-g15d3648df9/flash.img')
        image_hash = ('f96d11db521fe7a2787745e9e391225deeeec3318ee0fc07c8b799b8833dd474')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test');
        self.do_test_arm_aspeed_buildroot_start(image_path, '0x0')

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

    def test_arm_ast2600_evb_buildroot(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2600-evb
        """

        image_url = ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
                     'images/ast2600-evb/buildroot-2022.11-2-g15d3648df9/flash.img')
        image_hash = ('e598d86e5ea79671ca8b59212a326c911bc8bea728dec1a1f5390d717a28bb8b')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.3,address=0x4d,id=tmp-test');
        self.vm.add_args('-device',
                         'ds1338,bus=aspeed.i2c.bus.3,address=0x32');
        self.vm.add_args('-device',
                         'i2c-echo,bus=aspeed.i2c.bus.3,address=0x42');
        self.do_test_arm_aspeed_buildroot_start(image_path, '0xf00')

        exec_command_and_wait_for_pattern(self,
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-3/device/new_device',
             'i2c i2c-3: new_device: Instantiated device lm75 at 0x4d');
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon0/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000);
        exec_command_and_wait_for_pattern(self,
                             'cat /sys/class/hwmon/hwmon0/temp1_input', '18000')

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

    @skipUnless(*has_cmd('swtpm'))
    def test_arm_ast2600_evb_buildroot_tpm(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2600-evb
        """

        image_url = ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
                     'images/ast2600-evb/buildroot-2023.02-tpm/flash.img')
        image_hash = ('a46009ae8a5403a0826d607215e731a8c68d27c14c41e55331706b8f9c7bd997')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')

        # force creation of VM object, which also defines self._sd
        vm = self.vm

        socket = os.path.join(self._sd.name, 'swtpm-socket')

        subprocess.run(['swtpm', 'socket', '-d', '--tpm2',
                        '--tpmstate', f'dir={self.vm.temp_dir}',
                        '--ctrl', f'type=unixio,path={socket}'])

        self.vm.add_args('-chardev', f'socket,id=chrtpm,path={socket}')
        self.vm.add_args('-tpmdev', 'emulator,id=tpm0,chardev=chrtpm')
        self.vm.add_args('-device',
                         'tpm-tis-i2c,tpmdev=tpm0,bus=aspeed.i2c.bus.12,address=0x2e')
        self.do_test_arm_aspeed_buildroot_start(image_path, '0xf00', 'Aspeed AST2600 EVB')
        exec_command(self, "passw0rd")

        exec_command_and_wait_for_pattern(self,
            'echo tpm_tis_i2c 0x2e > /sys/bus/i2c/devices/i2c-12/new_device',
            'tpm_tis_i2c 12-002e: 2.0 TPM (device-id 0x1, rev-id 1)');
        exec_command_and_wait_for_pattern(self,
            'cat /sys/class/tpm/tpm0/pcr-sha256/0',
            'B804724EA13F52A9072BA87FE8FDCC497DFC9DF9AA15B9088694639C431688E0');

        self.do_test_arm_aspeed_buildroot_poweroff()

class AST2x00MachineSDK(QemuSystemTest, LinuxSSHMixIn):

    EXTRA_BOOTARGS = (
        'quiet '
        'systemd.mask=org.openbmc.HostIpmi.service '
        'systemd.mask=xyz.openbmc_project.Chassis.Control.Power@0.service '
        'systemd.mask=modprobe@fuse.service '
        'systemd.mask=rngd.service '
        'systemd.mask=obmc-console@ttyS2.service '
    )

    # FIXME: Although these tests boot a whole distro they are still
    # slower than comparable machine models. There may be some
    # optimisations which bring down the runtime. In the meantime they
    # have generous timeouts and are disable for CI which aims for all
    # tests to run in less than 60 seconds.
    timeout = 240

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def do_test_arm_aspeed_sdk_start(self, image):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user,hostfwd=:127.0.0.1:0-:22')
        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2019.04')
        interrupt_interactive_console_until_pattern(
            self, 'Hit any key to stop autoboot:', 'ast#')
        exec_command_and_wait_for_pattern(
            self, 'setenv bootargs ${bootargs} ' + self.EXTRA_BOOTARGS, 'ast#')
        exec_command_and_wait_for_pattern(
            self, 'boot', '## Loading kernel from FIT Image')
        self.wait_for_console_pattern('Starting kernel ...')

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

    def test_arm_ast2500_evb_sdk(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2500-evb
        :avocado: tags=flaky
        """

        image_url = ('https://github.com/AspeedTech-BMC/openbmc/releases/'
                     'download/v08.06/ast2500-default-obmc.tar.gz')
        image_hash = ('e1755f3cadff69190438c688d52dd0f0d399b70a1e14b1d3d5540fc4851d38ca')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')
        archive.extract(image_path, self.workdir)

        self.do_test_arm_aspeed_sdk_start(
            self.workdir + '/ast2500-default/image-bmc')
        self.wait_for_console_pattern('nodistro.0 ast2500-default ttyS4')

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')

    def test_arm_ast2600_evb_sdk(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:ast2600-evb
        :avocado: tags=flaky
        """

        image_url = ('https://github.com/AspeedTech-BMC/openbmc/releases/'
                     'download/v08.06/ast2600-a2-obmc.tar.gz')
        image_hash = ('9083506135f622d5e7351fcf7d4e1c7125cee5ba16141220c0ba88931f3681a4')
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')
        archive.extract(image_path, self.workdir)

        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.5,address=0x4d,id=tmp-test');
        self.vm.add_args('-device',
                         'ds1338,bus=aspeed.i2c.bus.5,address=0x32');
        self.do_test_arm_aspeed_sdk_start(
            self.workdir + '/ast2600-a2/image-bmc')
        self.wait_for_console_pattern('nodistro.0 ast2600-a2 ttyS4')

        self.ssh_connect('root', '0penBmc', False)
        self.ssh_command('dmesg -c > /dev/null')

        self.ssh_command_output_contains(
             'echo lm75 0x4d > /sys/class/i2c-dev/i2c-5/device/new_device ; '
             'dmesg -c',
             'i2c i2c-5: new_device: Instantiated device lm75 at 0x4d');
        self.ssh_command_output_contains(
                             'cat /sys/class/hwmon/hwmon19/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000);
        self.ssh_command_output_contains(
                             'cat /sys/class/hwmon/hwmon19/temp1_input', '18000')

        self.ssh_command_output_contains(
             'echo ds1307 0x32 > /sys/class/i2c-dev/i2c-5/device/new_device ; '
             'dmesg -c',
             'i2c i2c-5: new_device: Instantiated device ds1307 at 0x32');
        year = time.strftime("%Y")
        self.ssh_command_output_contains('/sbin/hwclock -f /dev/rtc1', year);
