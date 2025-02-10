#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright 2025, The QEMU Project Developers.
#
# A functional test that runs MicroPython on the arm microbit machine.

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class MicrobitMachine(QemuSystemTest):

    ASSET_MICRO = Asset('https://ozlabs.org/~joel/microbit-micropython.hex',
        '021641f93dfb11767d4978dbb3ca7f475d1b13c69e7f4aec3382f212636bffd6')

    def test_arm_microbit(self):
        self.set_machine('microbit')

        micropython = self.ASSET_MICRO.fetch()
        self.vm.set_console()
        self.vm.add_args('-device', f'loader,file={micropython}')
        self.vm.launch()
        wait_for_console_pattern(self, 'Type "help()" for more information.')
        exec_command_and_wait_for_pattern(self, 'import machine as mch', '>>>')
        exec_command_and_wait_for_pattern(self, 'mch.reset()', 'MicroPython')
        wait_for_console_pattern(self, '>>>')

if __name__ == '__main__':
    QemuSystemTest.main()
