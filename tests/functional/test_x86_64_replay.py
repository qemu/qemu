#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on x86_64 machines
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipFlakyTest
from replay_kernel import ReplayKernelBase


class X86Replay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
         ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
          '/releases/29/Everything/x86_64/os/images/pxeboot/vmlinuz'),
        '8f237d84712b1b411baf3af2aeaaee10b9aae8e345ec265b87ab3a39639eb143')

    def do_test_x86(self, machine):
        self.set_machine(machine)
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'VFS: Cannot open root device'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

    @skipFlakyTest('https://gitlab.com/qemu-project/qemu/-/issues/2094')
    def test_pc(self):
        self.do_test_x86('pc')

    def test_q35(self):
        self.do_test_x86('q35')


if __name__ == '__main__':
    ReplayKernelBase.main()
