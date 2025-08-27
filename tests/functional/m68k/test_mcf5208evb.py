#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an MCF5208EVB machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class Mcf5208EvbTest(LinuxKernelTest):

    ASSET_DAY07 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day07.tar.xz',
        '753c2f3837126b7c6ba92d0b1e0b156e8a2c5131d2d576bb0b9a763fae73c08a')

    def test_m68k_mcf5208evb(self):
        self.set_machine('mcf5208evb')
        self.archive_extract(self.ASSET_DAY07)
        self.vm.set_console()
        self.vm.add_args('-kernel',
                         self.scratch_file('day07', 'sanity-clause.elf'))
        self.vm.launch()
        self.wait_for_console_pattern('QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
