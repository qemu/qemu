#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on a sparc sun4m machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class SparcReplay(ReplayKernelBase):

    ASSET_DAY11 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day11.tar.xz',
        'c776533ba756bf4dd3f1fc4c024fb50ef0d853e05c5f5ddf0900a32d1eaa49e0')

    def test_replay(self):
        self.set_machine('SS-10')
        kernel_path = self.archive_extract(self.ASSET_DAY11,
                                           member="day11/zImage.elf")
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar')


if __name__ == '__main__':
    ReplayKernelBase.main()
