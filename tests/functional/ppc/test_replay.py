#!/usr/bin/env python3
#
# Replay tests for ppc machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class PpcReplay(ReplayKernelBase):

    ASSET_DAY15 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day15.tar.xz',
        '03e0757c131d2959decf293a3572d3b96c5a53587165bf05ce41b2818a2bccd5')

    def do_day15_test(self):
        self.require_accelerator("tcg")
        kernel_path = self.archive_extract(self.ASSET_DAY15,
                                           member='day15/invaders.elf')
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar', args=('-M', 'graphics=off'))

    def test_g3beige(self):
        self.set_machine('g3beige')
        self.do_day15_test()

    def test_mac99(self):
        self.set_machine('mac99')
        self.do_day15_test()


if __name__ == '__main__':
    ReplayKernelBase.main()
