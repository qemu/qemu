#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an OpenRISC-1000 SIM machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class Or1kReplay(ReplayKernelBase):

    ASSET_DAY20 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day20.tar.xz',
        'ff9d7dd7c6bdba325bd85ee85c02db61ff653e129558aeffe6aff55bffb6763a')

    def test_sim(self):
        self.set_machine('or1k-sim')
        kernel_path = self.archive_extract(self.ASSET_DAY20,
                                           member='day20/vmlinux')
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar')


if __name__ == '__main__':
    ReplayKernelBase.main()
