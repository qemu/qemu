#!/usr/bin/env python3
#
# OpenSBI boot test for RISC-V machines
#
# Copyright (c) 2022, Ventana Micro
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern

class RiscvOpenSBI(QemuSystemTest):

    timeout = 5

    def boot_opensbi(self):
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, 'Platform Name')
        wait_for_console_pattern(self, 'Boot HART MEDELEG')

    def test_riscv_spike(self):
        self.set_machine('spike')
        self.boot_opensbi()

    def test_riscv_sifive_u(self):
        self.set_machine('sifive_u')
        self.boot_opensbi()

    def test_riscv_virt(self):
        self.set_machine('virt')
        self.boot_opensbi()

if __name__ == '__main__':
    QemuSystemTest.main()
