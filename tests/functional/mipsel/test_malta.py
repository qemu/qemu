#!/usr/bin/env python3
#
# Functional tests for the little-endian 32-bit MIPS Malta board
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, LinuxKernelTest, Asset
from qemu_test import skipFlakyTest
from qemu_test import interrupt_interactive_console_until_pattern
from qemu_test import wait_for_console_pattern

from mips.test_malta import mips_check_wheezy


class MaltaMachineConsole(LinuxKernelTest):

    ASSET_KERNEL_4K = Asset(
        ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
         'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
         'generic_nano32r6el_page4k.xz'),
        '019e034094ac6cf3aa77df5e130fb023ce4dbc804b04bfcc560c6403e1ae6bdb')
    ASSET_KERNEL_16K = Asset(
        ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
         'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
         'generic_nano32r6el_page16k_up.xz'),
        '3a54a10b3108c16a448dca9ea3db378733a27423befc2a45a5bdf990bd85e12c')
    ASSET_KERNEL_64K = Asset(
        ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
         'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
         'generic_nano32r6el_page64k_dbg.xz'),
        'ce21ff4b07a981ecb8a39db2876616f5a2473eb2ab459c6f67465b9914b0c6b6')

    def do_test_mips_malta32el_nanomips(self, kernel):
        kernel_path = self.uncompress(kernel)

        self.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'mem=256m@@0x0 '
                               + 'console=ttyS0')
        self.vm.add_args('-cpu', 'I7200',
                         '-no-reboot',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips_malta32el_nanomips_4k(self):
        self.do_test_mips_malta32el_nanomips(self.ASSET_KERNEL_4K)

    def test_mips_malta32el_nanomips_16k_up(self):
        self.do_test_mips_malta32el_nanomips(self.ASSET_KERNEL_16K)

    def test_mips_malta32el_nanomips_64k_dbg(self):
        self.do_test_mips_malta32el_nanomips(self.ASSET_KERNEL_64K)

    ASSET_WHEEZY_KERNEL = Asset(
        ('https://people.debian.org/~aurel32/qemu/mipsel/'
         'vmlinux-3.2.0-4-4kc-malta'),
        'dc8a3648305b0201ca7a5cd135fe2890067a65d93c38728022bb0e656ad2bf9a')

    ASSET_WHEEZY_DISK = Asset(
        ('https://people.debian.org/~aurel32/qemu/mipsel/'
         'debian_wheezy_mipsel_standard.qcow2'),
        '454f09ae39f7e6461c84727b927100d2c7813841f2a0a5dce328114887ecf914')

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/3109")
    def test_wheezy(self):
        kernel_path = self.ASSET_WHEEZY_KERNEL.fetch()
        image_path = self.ASSET_WHEEZY_DISK.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 root=/dev/sda1')
        mips_check_wheezy(self,
            kernel_path, image_path, kernel_command_line,
            dl_file='/boot/initrd.img-3.2.0-4-4kc-malta',
            hsum='9fc9f250ed56a74e35e704ddfd5a1c5a5625adefc5c9da91f649288d3ca000f0')


class MaltaMachineYAMON(QemuSystemTest):

    ASSET_YAMON_ROM = Asset(
        ('https://s3-eu-west-1.amazonaws.com/downloads-mips/mips-downloads/'
         'YAMON/yamon-bin-02.22.zip'),
        'eef86f0eed0ef554f041dcd47b87eebea0e6f9f1184ed31f7e9e8b4a803860ab')

    def test_mipsel_malta_yamon(self):
        yamon_bin = 'yamon-02.22.bin'
        self.archive_extract(self.ASSET_YAMON_ROM)
        yamon_path = self.scratch_file(yamon_bin)

        self.set_machine('malta')
        self.vm.set_console()
        self.vm.add_args('-bios', yamon_path)
        self.vm.launch()

        prompt =  'YAMON>'
        pattern = 'YAMON ROM Monitor'
        interrupt_interactive_console_until_pattern(self, pattern, prompt)
        wait_for_console_pattern(self, prompt)
        self.vm.shutdown()


if __name__ == '__main__':
    QemuSystemTest.main()
