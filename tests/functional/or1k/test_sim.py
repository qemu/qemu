#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an OpenRISC-1000 SIM machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class OpenRISC1kSimTest(LinuxKernelTest):

    ASSET_DAY20 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day20.tar.xz',
        'ff9d7dd7c6bdba325bd85ee85c02db61ff653e129558aeffe6aff55bffb6763a')

    def test_or1k_sim(self):
        self.set_machine('or1k-sim')
        self.archive_extract(self.ASSET_DAY20)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.scratch_file('day20', 'vmlinux'))
        self.vm.launch()
        self.wait_for_console_pattern('QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
