#!/usr/bin/env python3
#
# Boot a Linux kernel on a r2d sh4 machine and check the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import LinuxKernelTest, Asset
from qemu_test.utils import archive_extract
from unittest import skipUnless

class R2dTest(LinuxKernelTest):

    ASSET_DAY09 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day09.tar.xz',
        'a61b44d2630a739d1380cc4ff4b80981d47ccfd5992f1484ccf48322c35f09ac')

    # This test has a 6-10% failure rate on various hosts that look
    # like issues with a buggy kernel.
    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable')
    def test_r2d(self):
        self.set_machine('r2d')
        file_path = self.ASSET_DAY09.fetch()
        archive_extract(file_path, self.workdir)
        self.vm.add_args('-append', 'console=ttySC1')
        self.launch_kernel(self.workdir + '/day09/zImage', console_index=1,
                           wait_for='QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
