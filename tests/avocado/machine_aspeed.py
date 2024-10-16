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

    def do_test_aarch64_aspeed_sdk_start(self, image):
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user,hostfwd=:127.0.0.1:0-:22')

        self.vm.launch()

        self.wait_for_console_pattern('U-Boot 2023.10')
        self.wait_for_console_pattern('## Loading kernel from FIT Image')
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

    def test_aarch64_ast2700_evb_sdk_v09_02(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:ast2700-evb
        """

        image_url = ('https://github.com/AspeedTech-BMC/openbmc/releases/'
                     'download/v09.02/ast2700-default-obmc.tar.gz')
        image_hash = 'ac969c2602f4e6bdb69562ff466b89ae3fe1d86e1f6797bb7969d787f82116a7'
        image_path = self.fetch_asset(image_url, asset_hash=image_hash,
                                      algorithm='sha256')
        archive.extract(image_path, self.workdir)

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
        self.wait_for_console_pattern('nodistro.0 ast2700-default ttyS12')

        self.ssh_connect('root', '0penBmc', False)
        self.ssh_command('dmesg -c > /dev/null')

        self.ssh_command_output_contains(
            'echo lm75 0x4d > /sys/class/i2c-dev/i2c-1/device/new_device '
            '&& dmesg -c',
            'i2c i2c-1: new_device: Instantiated device lm75 at 0x4d');

        self.ssh_command_output_contains(
            'cat /sys/class/hwmon/hwmon20/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000)
        self.ssh_command_output_contains(
            'cat /sys/class/hwmon/hwmon20/temp1_input', '18000')
