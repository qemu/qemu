#!/usr/bin/env python3
#
# Replay test that boots a Linux kernel on an s390x machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from replay_kernel import ReplayKernelBase


class S390xReplay(ReplayKernelBase):

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora-secondary/'
         'releases/29/Everything/s390x/os/images/kernel.img'),
        'dace03b8ae0c9f670ebb9b8d6ce5eb24b62987f346de8f1300a439bb00bb99e7')

    def test_s390_ccw_virtio(self):
        self.set_machine('s390-ccw-virtio')
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=sclp0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=9)


if __name__ == '__main__':
    ReplayKernelBase.main()
