#!/usr/bin/env python3
#
# Functional test that checks the serial console of the stellaris machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class StellarisMachine(QemuSystemTest):

    ASSET_DAY22 = Asset(
        'https://www.qemu-advent-calendar.org/2023/download/day22.tar.gz',
        'ae3a63ef4b7a22c21bfc7fc0d85e402fe95e223308ed23ac854405016431ff51')

    def test_lm3s6965evb(self):
        self.set_machine('lm3s6965evb')
        kernel_path = self.archive_extract(self.ASSET_DAY22,
                                           member='day22/day22.bin')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)
        self.vm.launch()

        wait_for_console_pattern(self, 'In a one horse open')

    ASSET_NOTMAIN = Asset(
        'https://github.com/Ahelion/QemuArmM4FDemoSw/raw/master/build/notmain.bin',
        '6ceda031aa081a420fca2fca9e137fa681d6e3820d820ad1917736cb265e611a')

    def test_lm3s811evb(self):
        self.set_machine('lm3s811evb')
        kernel_path = self.ASSET_NOTMAIN.fetch()

        self.vm.set_console()
        self.vm.add_args('-cpu', 'cortex-m4')
        self.vm.add_args('-kernel', kernel_path)
        self.vm.launch()

        # The test kernel emits an initial '!' and then waits for input.
        # For each character that we send it responds with a certain
        # other ASCII character.
        wait_for_console_pattern(self, '!')
        exec_command_and_wait_for_pattern(self, '789', 'cdf')


if __name__ == '__main__':
    QemuSystemTest.main()
