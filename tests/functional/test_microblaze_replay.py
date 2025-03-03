#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an microblaze machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class MicroblazeReplay(ReplayKernelBase):

    ASSET_DAY17 = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day17.tar.xz'),
        '3ba7439dfbea7af4876662c97f8e1f0cdad9231fc166e4861d17042489270057')

    def test_microblaze_s3adsp1800(self):
        self.set_machine('petalogix-s3adsp1800')
        kernel_path = self.archive_extract(self.ASSET_DAY17,
                                           member='day17/ballerina.bin')
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar')


if __name__ == '__main__':
    ReplayKernelBase.main()
