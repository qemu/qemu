#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on ppc64 machines
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipFlakyTest
from replay_kernel import ReplayKernelBase


class Ppc64Replay(ReplayKernelBase):

    ASSET_DAY19 = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day19.tar.xz'),
        '20b1bb5a8488c664defbb5d283addc91a05335a936c63b3f5ff7eee74b725755')

    @skipFlakyTest('https://gitlab.com/qemu-project/qemu/-/issues/2523')
    def test_ppc64_e500(self):
        self.set_machine('ppce500')
        self.cpu = 'e5500'
        kernel_path = self.archive_extract(self.ASSET_DAY19,
                                           member='day19/uImage')
        self.run_rr(kernel_path, self.REPLAY_KERNEL_COMMAND_LINE,
                    'QEMU advent calendar')

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora-secondary/'
         'releases/29/Everything/ppc64le/os/ppc/ppc64/vmlinuz'),
        '383c2f5c23bc0d9d32680c3924d3fd7ee25cc5ef97091ac1aa5e1d853422fc5f')

    def test_ppc64_pseries(self):
        self.set_machine('pseries')
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=hvc0'
        console_pattern = 'VFS: Cannot open root device'
        self.run_rr(kernel_path, kernel_command_line, console_pattern)

    def test_ppc64_powernv(self):
        self.set_machine('powernv')
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + \
                              'console=tty0 console=hvc0'
        console_pattern = 'VFS: Cannot open root device'
        self.run_rr(kernel_path, kernel_command_line, console_pattern)


if __name__ == '__main__':
    ReplayKernelBase.main()
