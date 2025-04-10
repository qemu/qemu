#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an aarch64 machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipIfOperatingSystem
from replay_kernel import ReplayKernelBase


class Aarch64Replay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/29/Everything/aarch64/os/images/pxeboot/vmlinuz'),
        '7e1430b81c26bdd0da025eeb8fbd77b5dc961da4364af26e771bd39f379cbbf7')

    def test_aarch64_virt(self):
        self.set_machine('virt')
        self.cpu = 'cortex-a53'
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'VFS: Cannot open root device'
        self.run_rr(kernel_path, kernel_command_line, console_pattern)


if __name__ == '__main__':
    ReplayKernelBase.main()
