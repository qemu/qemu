#!/usr/bin/env python3
#
# CD boot test for HPPA machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class HppaCdBoot(QemuSystemTest):

    ASSET_CD = Asset(
        ('https://github.com/philmd/qemu-testing-blob/raw/ec1b741/'
         'hppa/hp9000/712/C7120023.frm'),
        '32c612ad2074516986bdc27768903c561fa92af2ca48e5ac3f3359ade1c42f70')

    def test_cdboot(self):
        self.set_machine('B160L')
        cdrom_path = self.ASSET_CD.fetch()

        self.vm.set_console()
        self.vm.add_args('-cdrom', cdrom_path,
                         '-boot', 'd',
                         '-no-reboot')
        self.vm.launch()
        wait_for_console_pattern(self, 'Unrecognized MODEL TYPE = 502')
        wait_for_console_pattern(self, 'UPDATE PAUSED>')

        exec_command_and_wait_for_pattern(self, 'exit\r', 'UPDATE>')
        exec_command_and_wait_for_pattern(self, 'ls\r', 'IMAGE1B')
        wait_for_console_pattern(self, 'UPDATE>')
        exec_command_and_wait_for_pattern(self, 'exit\r',
                        'THIS UTILITY WILL NOW RESET THE SYSTEM.....')


if __name__ == '__main__':
    QemuSystemTest.main()
