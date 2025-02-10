#!/usr/bin/env python3
#
# SeaBIOS boot test for HPPA machines
#
# Copyright (c) 2024 Linaro, Ltd
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern

class HppaSeabios(QemuSystemTest):

    timeout = 5
    MACH_BITS = {'B160L': 32, 'C3700': 64}

    def boot_seabios(self):
        mach = self.machine
        bits = self.MACH_BITS[mach]
        self.vm.set_console()
        self.vm.launch()
        self.machine
        wait_for_console_pattern(self, f'SeaBIOS PA-RISC {bits}-bit Firmware')
        wait_for_console_pattern(self, f'Emulated machine:     HP {mach} ({bits}-bit')

    def test_hppa_32(self):
        self.set_machine('B160L')
        self.boot_seabios()

    def test_hppa_64(self):
        self.set_machine('C3700')
        self.boot_seabios()

if __name__ == '__main__':
    QemuSystemTest.main()
