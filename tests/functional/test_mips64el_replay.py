#!/usr/bin/env python3
#
# Replay tests for the little-endian 64-bit MIPS Malta board
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipUntrustedTest
from replay_kernel import ReplayKernelBase


class Mips64elReplay(ReplayKernelBase):

    ASSET_KERNEL_2_63_2 = Asset(
        ('http://snapshot.debian.org/archive/debian/'
         '20130217T032700Z/pool/main/l/linux-2.6/'
         'linux-image-2.6.32-5-5kc-malta_2.6.32-48_mipsel.deb'),
        '35eb476f03be589824b0310358f1c447d85e645b88cbcd2ac02b97ef560f9f8d')

    def test_replay_mips64el_malta(self):
        self.set_machine('malta')
        kernel_path = self.archive_extract(self.ASSET_KERNEL_2_63_2,
                                    member='boot/vmlinux-2.6.32-5-5kc-malta')
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)


    ASSET_KERNEL_3_19_3 = Asset(
        ('https://github.com/philmd/qemu-testing-blob/'
         'raw/9ad2df38/mips/malta/mips64el/'
         'vmlinux-3.19.3.mtoman.20150408'),
        '8d3beb003bc66051ead98e7172139017fcf9ce2172576541c57e86418dfa5ab8')

    ASSET_CPIO_R1 = Asset(
        ('https://github.com/groeck/linux-build-test/'
         'raw/8584a59e/rootfs/mipsel64/'
         'rootfs.mipsel64r1.cpio.gz'),
        '75ba10cd35fb44e32948eeb26974f061b703c81c4ba2fab1ebcacf1d1bec3b61')

    @skipUntrustedTest()
    def test_replay_mips64el_malta_5KEc_cpio(self):
        self.set_machine('malta')
        self.cpu = '5KEc'
        kernel_path = self.ASSET_KERNEL_3_19_3.fetch()
        initrd_path = self.uncompress(self.ASSET_CPIO_R1)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 console=tty '
                               'rdinit=/sbin/init noreboot')
        console_pattern = 'Boot successful.'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5,
                    args=('-initrd', initrd_path))


if __name__ == '__main__':
    ReplayKernelBase.main()
