#!/usr/bin/env python3
#
# Functional tests for the big-endian 64-bit MIPS Malta board
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from test_mips_malta import mips_check_wheezy


class MaltaMachineConsole(LinuxKernelTest):

    ASSET_WHEEZY_KERNEL = Asset(
        ('https://people.debian.org/~aurel32/qemu/mips/'
         'vmlinux-3.2.0-4-5kc-malta'),
        '3e4ec154db080b3f1839f04dde83120654a33e5e1716863de576c47cb94f68f6')

    ASSET_WHEEZY_DISK = Asset(
        ('https://people.debian.org/~aurel32/qemu/mips/'
         'debian_wheezy_mips_standard.qcow2'),
        'de03599285b8382ad309309a6c4869f6c6c42a5cfc983342bab9ec0dfa7849a2')

    def test_wheezy(self):
        kernel_path = self.ASSET_WHEEZY_KERNEL.fetch()
        image_path = self.ASSET_WHEEZY_DISK.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 root=/dev/sda1')
        mips_check_wheezy(self,
            kernel_path, image_path, kernel_command_line, cpuinfo='MIPS 20Kc',
            dl_file='/boot/initrd.img-3.2.0-4-5kc-malta',
            hsum='d98b953bb4a41c0fc0fd8d19bbc691c08989ac52568c1d3054d92dfd890d3f06')


if __name__ == '__main__':
    LinuxKernelTest.main()
