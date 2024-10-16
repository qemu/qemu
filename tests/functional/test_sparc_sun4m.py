#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a sparc sun4m machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test.utils import archive_extract

class Sun4mTest(LinuxKernelTest):

    ASSET_DAY11 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day11.tar.xz',
        'c776533ba756bf4dd3f1fc4c024fb50ef0d853e05c5f5ddf0900a32d1eaa49e0')

    def test_sparc_ss20(self):
        self.set_machine('SS-20')
        file_path = self.ASSET_DAY11.fetch()
        archive_extract(file_path, self.workdir)
        self.launch_kernel(self.workdir + '/day11/zImage.elf',
                           wait_for='QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
