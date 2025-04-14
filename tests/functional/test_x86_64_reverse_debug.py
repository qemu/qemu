#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import skipIfMissingImports, skipFlakyTest
from reverse_debugging import ReverseDebugging


@skipIfMissingImports('avocado.utils')
class ReverseDebugging_X86_64(ReverseDebugging):

    REG_PC = 0x10
    REG_CS = 0x12
    def get_pc(self, g):
        return self.get_reg_le(g, self.REG_PC) \
            + self.get_reg_le(g, self.REG_CS) * 0x10

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/2922")
    def test_x86_64_pc(self):
        self.set_machine('pc')
        # start with BIOS only
        self.reverse_debugging()


if __name__ == '__main__':
    ReverseDebugging.main()
