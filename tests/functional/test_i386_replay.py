#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on a i386 machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class I386Replay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
         'https://storage.tuxboot.com/20230331/i386/bzImage',
        'a3e5b32a354729e65910f5a1ffcda7c14a6c12a55e8213fb86e277f1b76ed956')

    def test_pc(self):
        self.set_machine('pc')
        kernel_url = ()
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'VFS: Cannot open root device'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)


if __name__ == '__main__':
    ReplayKernelBase.main()
