#!/usr/bin/env python3
#
# QEMU AVR Arduino UNO functional test
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


class UnoMachine(QemuSystemTest):

    ASSET_UNO = Asset(
        ('https://github.com/RahulRNandan/LED_Blink_AVR/raw/'
         'c6d602cbb974a193/build/main.elf'),
        '3009a4e2cf5c5b65142f538abdf66d4dc6bc6beab7e552fff9ae314583761b72')

    def test_uno(self):
        """
        The binary constantly prints out 'LED Blink'
        """
        self.set_machine('arduino-uno')
        rom_path = self.ASSET_UNO.fetch()

        self.vm.add_args('-bios', rom_path)
        self.vm.set_console()
        self.vm.launch()

        wait_for_console_pattern(self, 'LED Blink')


if __name__ == '__main__':
    QemuSystemTest.main()
