#!/usr/bin/env python3
#
# Replay tests for the little-endian 32-bit MIPS Malta board
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipSlowTest
from replay_kernel import ReplayKernelBase


class MipselReplay(ReplayKernelBase):

    ASSET_KERNEL_4K = Asset(
        ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
         'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
         'generic_nano32r6el_page4k.xz'),
        '019e034094ac6cf3aa77df5e130fb023ce4dbc804b04bfcc560c6403e1ae6bdb')
    ASSET_KERNEL_16K = Asset(
        ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
         'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
         'generic_nano32r6el_page16k_up.xz'),
        '3a54a10b3108c16a448dca9ea3db378733a27423befc2a45a5bdf990bd85e12c')
    ASSET_KERNEL_64K = Asset(
        ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
         'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
         'generic_nano32r6el_page64k_dbg.xz'),
        'ce21ff4b07a981ecb8a39db2876616f5a2473eb2ab459c6f67465b9914b0c6b6')

    def do_test_replay_mips_malta32el_nanomips(self, kernel_asset):
        self.set_machine('malta')
        self.cpu = 'I7200'
        kernel_path = self.uncompress(kernel_asset)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'mem=256m@@0x0 '
                               'console=ttyS0')
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

    @skipSlowTest()
    def test_replay_mips_malta32el_nanomips_4k(self):
        self.do_test_replay_mips_malta32el_nanomips(self.ASSET_KERNEL_4K)

    @skipSlowTest()
    def test_replay_mips_malta32el_nanomips_16k_up(self):
        self.do_test_replay_mips_malta32el_nanomips(self.ASSET_KERNEL_16K)

    @skipSlowTest()
    def test_replay_mips_malta32el_nanomips_64k_dbg(self):
        self.do_test_replay_mips_malta32el_nanomips(self.ASSET_KERNEL_64K)


if __name__ == '__main__':
    ReplayKernelBase.main()
