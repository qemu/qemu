#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test for x86_64
#
# Copyright (c) 2020 ISP RAS
# Copyright (c) 2025 Linaro Limited
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#  Gustavo Romero <gustavo.romero@linaro.org> (Run without Avocado)
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import skipFlakyTest
from reverse_debugging import ReverseDebugging


class ReverseDebuggingX86(ReverseDebugging):

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/2922")
    def test_x86_64_pc(self):
        self.set_machine('pc')
        # start with BIOS only
        self.reverse_debugging(gdb_arch='x86-64')


if __name__ == '__main__':
    ReverseDebugging.main()
