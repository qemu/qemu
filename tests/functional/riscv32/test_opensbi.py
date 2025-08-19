#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reuse the 64-bit OpenSBI test for RISC-V 32-bit machines

from riscv64.test_opensbi import RiscvOpenSBI

if __name__ == '__main__':
    RiscvOpenSBI.main()
