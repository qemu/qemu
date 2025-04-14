#!/usr/bin/env python3
#
# Replay tests for the big-endian 32-bit MIPS Malta board
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipSlowTest
from replay_kernel import ReplayKernelBase


class MipsReplay(ReplayKernelBase):

    ASSET_KERNEL_2_63_2 = Asset(
        ('http://snapshot.debian.org/archive/debian/'
         '20130217T032700Z/pool/main/l/linux-2.6/'
         'linux-image-2.6.32-5-4kc-malta_2.6.32-48_mips.deb'),
        '16ca524148afb0626f483163e5edf352bc1ab0e4fc7b9f9d473252762f2c7a43')

    def test_replay_mips_malta(self):
        self.set_machine('malta')
        kernel_path = self.archive_extract(self.ASSET_KERNEL_2_63_2,
                                     member='boot/vmlinux-2.6.32-5-4kc-malta')
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

    ASSET_KERNEL_4_5_0 = Asset(
        ('http://snapshot.debian.org/archive/debian/'
         '20160601T041800Z/pool/main/l/linux/'
         'linux-image-4.5.0-2-4kc-malta_4.5.5-1_mips.deb'),
        '526b17d5889840888b76fc2c36a0ebde182c9b1410a3a1e68203c3b160eb2027')

    ASSET_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '8584a59ed9e5eb5ee7ca91f6d74bbb06619205b8/rootfs/'
         'mips/rootfs.cpio.gz'),
        'dcfe3a7fe3200da3a00d176b95caaa086495eb158f2bff64afc67d7e1eb2cddc')

    @skipSlowTest()
    def test_replay_mips_malta_cpio(self):
        self.set_machine('malta')
        kernel_path = self.archive_extract(self.ASSET_KERNEL_4_5_0,
                                      member='boot/vmlinux-4.5.0-2-4kc-malta')
        initrd_path = self.uncompress(self.ASSET_INITRD)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 console=tty '
                               'rdinit=/sbin/init noreboot')
        console_pattern = 'Boot successful.'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5,
                    args=('-initrd', initrd_path))


if __name__ == '__main__':
    ReplayKernelBase.main()
