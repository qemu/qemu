#!/usr/bin/env python3
#
# Boot a Linux kernel on a e500 ppc64 machine and check the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class E500Test(LinuxKernelTest):

    ASSET_DAY19 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day19.tar.xz',
        '20b1bb5a8488c664defbb5d283addc91a05335a936c63b3f5ff7eee74b725755')

    def test_ppc64_e500(self):
        self.set_machine('ppce500')
        self.cpu = 'e5500'
        self.archive_extract(self.ASSET_DAY19)
        self.launch_kernel(self.scratch_file('day19', 'uImage'),
                           wait_for='QEMU advent calendar')

if __name__ == '__main__':
    LinuxKernelTest.main()
