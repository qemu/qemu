#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an versatile express machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class VExpressTest(LinuxKernelTest):

    ASSET_DAY16 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day16.tar.xz',
        '63311adb2d4c4e7a73214a86d29988add87266a909719c56acfadd026b4110a7')

    def test_arm_vexpressa9(self):
        self.set_machine('vexpress-a9')
        self.archive_extract(self.ASSET_DAY16)
        self.launch_kernel(self.scratch_file('day16', 'winter.zImage'),
                           dtb=self.scratch_file('day16',
                                                 'vexpress-v2p-ca9.dtb'),
                           wait_for='QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
