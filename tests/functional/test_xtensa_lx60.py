#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an xtensa lx650 machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class XTensaLX60Test(LinuxKernelTest):

    ASSET_DAY02 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day02.tar.xz',
        '68ff07f9b3fd3df36d015eb46299ba44748e94bfbb2d5295fddc1a8d4a9fd324')

    def test_xtensa_lx60(self):
        self.set_machine('lx60')
        self.cpu = 'dc233c'
        self.archive_extract(self.ASSET_DAY02)
        self.launch_kernel(self.scratch_file('day02',
                                             'santas-sleigh-ride.elf'),
                           wait_for='QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
