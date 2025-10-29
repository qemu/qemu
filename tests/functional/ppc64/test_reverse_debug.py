#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test for ppc64
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


class ReverseDebuggingPpc64(ReverseDebugging):

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/1992")
    def test_ppc64_pseries(self):
        self.set_machine('pseries')
        # SLOF branches back to its entry point, which causes this test
        # to take the 'hit a breakpoint again' path. That's not a problem,
        # just slightly different than the other machines.
        self.reverse_debugging(gdb_arch='powerpc:common64')

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/1992")
    def test_ppc64_powernv(self):
        self.set_machine('powernv')
        self.reverse_debugging(gdb_arch='powerpc:common64')


if __name__ == '__main__':
    ReverseDebugging.main()
