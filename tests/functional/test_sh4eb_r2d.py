#!/usr/bin/env python3
#
# Boot a Linux kernel on a r2d sh4eb machine and check the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class R2dEBTest(LinuxKernelTest):

    ASSET_TGZ = Asset(
        'https://landley.net/bin/mkroot/0.8.11/sh4eb.tgz',
        'be8c6cb5aef8406899dc5aa5e22b6aa45840eb886cdd3ced51555c10577ada2c')

    def test_sh4eb_r2d(self):
        self.set_machine('r2d')
        self.archive_extract(self.ASSET_TGZ)
        self.vm.add_args('-append', 'console=ttySC1 noiotrap')
        self.launch_kernel(self.scratch_file('sh4eb', 'linux-kernel'),
                           initrd=self.scratch_file('sh4eb',
                                                    'initramfs.cpio.gz'),
                           console_index=1, wait_for='Type exit when done')
        exec_command_and_wait_for_pattern(self, 'exit', 'Restarting system')

if __name__ == '__main__':
    LinuxKernelTest.main()
