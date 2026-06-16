#!/usr/bin/env python3
#
# Functional tests for RISC-V big-endian support
#
# Copyright (c) 2026 MIPS
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class RiscvBigEndian(QemuSystemTest):
    """
    Tests for RISC-V runtime big-endian data support.

    Uses a bare-metal RV64 ELF that detects data endianness at runtime
    by storing a 32-bit word and reading back byte 0. Prints "ENDIAN: BE"
    or "ENDIAN: LE" to the NS16550A UART on the virt machine.
    """

    timeout = 10

    ASSET_BE_TEST = Asset(
        'https://github.com/MIPS/linux-test-downloads/raw/main/'
        'riscvbe-baremetal/be-test-bare-metal.elf',
        '9ad51b675e101de65908fadbac064ed1d0564c17463715d09dd734db86ea0f58')

    def _run_bare_metal(self, big_endian=False):
        self.set_machine('virt')
        kernel = self.ASSET_BE_TEST.fetch()
        self.vm.add_args('-bios', 'none')
        self.vm.add_args('-kernel', kernel)
        if big_endian:
            self.vm.add_args('-cpu', 'rv64,big-endian=on')
        self.vm.set_console()
        self.vm.launch()
        expected = 'ENDIAN: BE' if big_endian else 'ENDIAN: LE'
        wait_for_console_pattern(self, expected)

    def test_bare_metal_littleendian(self):
        """
        Boot bare-metal ELF on virt with default little-endian CPU.
        Expects "ENDIAN: LE" on UART.
        """
        self._run_bare_metal(big_endian=False)

    def test_bare_metal_bigendian(self):
        """
        Boot bare-metal ELF on virt with big-endian=on CPU property.
        Expects "ENDIAN: BE" on UART.
        """
        self._run_bare_metal(big_endian=True)


if __name__ == '__main__':
    QemuSystemTest.main()
