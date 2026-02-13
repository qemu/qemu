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
from qemu_test import exec_command_and_wait_for_pattern, skipIfMissingCommands


class AST2600Machine(AspeedTest):

    ASSET_BR2_202302_AST2600_TPM_FLASH = Asset(
        ('https://github.com/legoater/qemu-aspeed-boot/raw/master/'
         'images/ast2600-evb/buildroot-2023.02-tpm/flash.img'),
        'a46009ae8a5403a0826d607215e731a8c68d27c14c41e55331706b8f9c7bd997')

    def _test_arm_ast2600_evb_buildroot_tpm(self, tpmstate_dir):
        image_path = self.ASSET_BR2_202302_AST2600_TPM_FLASH.fetch()

        socket = os.path.join(tpmstate_dir, 'swtpm-socket')

        # We must put the TPM state dir in /tmp/, not the build dir,
        # because some distros use AppArmor to lock down swtpm and
        # restrict the set of locations it can access files in.
        subprocess.run(['swtpm', 'socket', '-d', '--tpm2',
                        '--tpmstate', f'dir={tpmstate_dir}',
                        '--ctrl', f'type=unixio,path={socket}'],
                       check=True)

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

    @skipIfMissingCommands('swtpm')
    def test_arm_ast2600_evb_buildroot_tpm(self):
        self.set_machine('ast2600-evb')
        with tempfile.TemporaryDirectory(prefix="qemu_") as tpmstate_dir:
            self._test_arm_ast2600_evb_buildroot_tpm(tpmstate_dir)


if __name__ == '__main__':
    AspeedTest.main()
