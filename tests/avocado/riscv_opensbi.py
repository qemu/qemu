# OpenSBI boot test for RISC-V machines
#
# Copyright (c) 2022, Ventana Micro
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class RiscvOpenSBI(QemuSystemTest):
    """
    :avocado: tags=accel:tcg
    """
    timeout = 5

    def boot_opensbi(self):
        self.vm.set_console()
        self.vm.launch()
        wait_for_console_pattern(self, 'Platform Name')
        wait_for_console_pattern(self, 'Boot HART MEDELEG')

    def test_riscv32_spike(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:spike
        """
        self.boot_opensbi()

    def test_riscv64_spike(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:spike
        """
        self.boot_opensbi()

    def test_riscv32_sifive_u(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:sifive_u
        """
        self.boot_opensbi()

    def test_riscv64_sifive_u(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:sifive_u
        """
        self.boot_opensbi()

    def test_riscv32_virt(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:virt
        """
        self.boot_opensbi()

    def test_riscv64_virt(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:virt
        """
        self.boot_opensbi()
