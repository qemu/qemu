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

import time

from avocado_qemu import QemuSystemTest

class AVR6Machine(QemuSystemTest):
    timeout = 5

    def test_freertos(self):
        """
        :avocado: tags=arch:avr
        :avocado: tags=machine:arduino-mega-2560-v3
        """
        """
        https://github.com/seharris/qemu-avr-tests/raw/master/free-rtos/Demo/AVR_ATMega2560_GCC/demo.elf
        constantly prints out 'ABCDEFGHIJKLMNOPQRSTUVWXABCDEFGHIJKLMNOPQRSTUVWX'
        """
        rom_url = ('https://github.com/seharris/qemu-avr-tests'
                   '/raw/36c3e67b8755dcf/free-rtos/Demo'
                   '/AVR_ATMega2560_GCC/demo.elf')
        rom_hash = '7eb521f511ca8f2622e0a3c5e8dd686efbb911d4'
        rom_path = self.fetch_asset(rom_url, asset_hash=rom_hash)

        self.vm.add_args('-bios', rom_path)
        self.vm.add_args('-nographic')
        self.vm.launch()

        time.sleep(2)
        self.vm.shutdown()

        self.assertIn('ABCDEFGHIJKLMNOPQRSTUVWXABCDEFGHIJKLMNOPQRSTUVWX',
                self.vm.get_log())
