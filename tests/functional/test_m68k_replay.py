#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an m68k machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class M68kReplay(ReplayKernelBase):

    ASSET_Q800 = Asset(
        ('https://snapshot.debian.org/'
         'archive/debian-ports/20191021T083923Z/pool-m68k/main/l/linux/'
         'kernel-image-5.3.0-1-m68k-di_5.3.7-1_m68k.udeb'),
        '949e50d74d4b9bc15d26c06d402717b7a4c0e32ff8100014f5930d8024de7b73')

    def test_q800(self):
        self.set_machine('q800')
        kernel_path = self.archive_extract(self.ASSET_Q800,
                                           member='boot/vmlinux-5.3.0-1-m68k')
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 vga=off')
        console_pattern = 'No filesystem could mount root'
        self.run_rr(kernel_path, kernel_command_line, console_pattern)

    ASSET_MCF5208 = Asset(
       'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day07.tar.xz',
       '753c2f3837126b7c6ba92d0b1e0b156e8a2c5131d2d576bb0b9a763fae73c08a')

    def test_mcf5208evb(self):
        self.set_machine('mcf5208evb')
        kernel_path = self.archive_extract(self.ASSET_MCF5208,
                                           member='day07/sanity-clause.elf')
        self.run_rr(kernel_path, self.KERNEL_COMMON_COMMAND_LINE,
                    'QEMU advent calendar')


if __name__ == '__main__':
    ReplayKernelBase.main()
