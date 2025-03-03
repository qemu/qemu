#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an Alpha machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class AlphaReplay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
        ('http://archive.debian.org/debian/dists/lenny/main/installer-alpha/'
         '20090123lenny10/images/cdrom/vmlinuz'),
        '34f53da3fa32212e4f00b03cb944b2ad81c06bc8faaf9b7193b2e544ceeca576')

    def test_clipper(self):
        self.set_machine('clipper')
        kernel_path = self.uncompress(self.ASSET_KERNEL, format='gz')
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=9,
            args=('-nodefaults', ))


if __name__ == '__main__':
    ReplayKernelBase.main()
