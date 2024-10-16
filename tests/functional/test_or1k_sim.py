#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an OpenRISC-1000 SIM machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import LinuxKernelTest, Asset
from qemu_test.utils import archive_extract

class OpenRISC1kSimTest(LinuxKernelTest):

    ASSET_DAY20 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day20.tar.xz',
        'ff9d7dd7c6bdba325bd85ee85c02db61ff653e129558aeffe6aff55bffb6763a')

    def test_or1k_sim(self):
        self.set_machine('or1k-sim')
        file_path = self.ASSET_DAY20.fetch()
        archive_extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/day20/vmlinux')
        self.vm.launch()
        self.wait_for_console_pattern('QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
