#!/usr/bin/env python3
#
# QEMU AVR integration tests
#
# Copyright (c) 2019-2020 Michael Rolnik <mrolnik@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


class AVR6Machine(QemuSystemTest):

    ASSET_ROM = Asset(('https://github.com/seharris/qemu-avr-tests'
                       '/raw/36c3e67b8755dcf/free-rtos/Demo'
                       '/AVR_ATMega2560_GCC/demo.elf'),
                      'ee4833bd65fc69e84a79ed1c608affddbd499a60e63acf87d9113618401904e4')

    def test_freertos(self):
        """
        https://github.com/seharris/qemu-avr-tests/raw/master/free-rtos/Demo/AVR_ATMega2560_GCC/demo.elf
        constantly prints out 'ABCDEFGHIJKLMNOPQRSTUVWXABCDEFGHIJKLMNOPQRSTUVWX'
        """
        rom_path = self.ASSET_ROM.fetch()

        self.set_machine('arduino-mega-2560-v3')
        self.vm.add_args('-bios', rom_path)
        self.vm.add_args('-nographic')
        self.vm.set_console()
        self.vm.launch()

        wait_for_console_pattern(self,
                        'XABCDEFGHIJKLMNOPQRSTUVWXABCDEFGHIJKLMNOPQRSTUVWXA')


if __name__ == '__main__':
    QemuSystemTest.main()
