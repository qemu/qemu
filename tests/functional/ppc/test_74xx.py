#!/usr/bin/env python3
#
# Smoke tests for 74xx cpus (aka G4).
#
# Copyright (c) 2021, IBM Corp.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern

class Ppc74xxCpu(QemuSystemTest):

    timeout = 5

    def test_ppc_7400(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7400')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7410(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7410')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,74xx')

    def test_ppc_7441(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7441')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7445(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7445')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7447(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7447')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7447a(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7447a')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7448(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7448')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,MPC86xx')

    def test_ppc_7450(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7450')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7451(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7451')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7455(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7455')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7457(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7457')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

    def test_ppc_7457a(self):
        self.require_accelerator("tcg")
        self.set_machine('g3beige')
        self.vm.set_console()
        self.vm.add_args('-cpu', '7457a')
        self.vm.launch()
        wait_for_console_pattern(self, '>> OpenBIOS')
        wait_for_console_pattern(self, '>> CPU type PowerPC,G4')

if __name__ == '__main__':
    QemuSystemTest.main()
