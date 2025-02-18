#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an xtensa lx650 machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class XTensaReplay(ReplayKernelBase):

    ASSET_DAY02 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day02.tar.xz',
        '68ff07f9b3fd3df36d015eb46299ba44748e94bfbb2d5295fddc1a8d4a9fd324')

    def test_replay(self):
        self.set_machine('lx60')
        self.cpu = 'dc233c'
        kernel_path = self.archive_extract(self.ASSET_DAY02,
                                         member='day02/santas-sleigh-ride.elf')
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar')


if __name__ == '__main__':
    ReplayKernelBase.main()
