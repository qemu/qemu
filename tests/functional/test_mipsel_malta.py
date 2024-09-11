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

import os

from qemu_test import QemuSystemTest, LinuxKernelTest, Asset
from qemu_test import interrupt_interactive_console_until_pattern
from qemu_test import wait_for_console_pattern
from qemu_test.utils import lzma_uncompress
from zipfile import ZipFile


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

    def do_test_mips_malta32el_nanomips(self, kernel_path_xz):
        kernel_path = os.path.join(self.workdir, 'kernel')
        lzma_uncompress(kernel_path_xz, kernel_path)

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
        kernel_path_xz = self.ASSET_KERNEL_4K.fetch()
        self.do_test_mips_malta32el_nanomips(kernel_path_xz)

    def test_mips_malta32el_nanomips_16k_up(self):
        kernel_path_xz = self.ASSET_KERNEL_16K.fetch()
        self.do_test_mips_malta32el_nanomips(kernel_path_xz)

    def test_mips_malta32el_nanomips_64k_dbg(self):
        kernel_path_xz = self.ASSET_KERNEL_16K.fetch()
        self.do_test_mips_malta32el_nanomips(kernel_path_xz)


class MaltaMachineYAMON(QemuSystemTest):

    ASSET_YAMON_ROM = Asset(
        ('https://s3-eu-west-1.amazonaws.com/downloads-mips/mips-downloads/'
         'YAMON/yamon-bin-02.22.zip'),
        'eef86f0eed0ef554f041dcd47b87eebea0e6f9f1184ed31f7e9e8b4a803860ab')

    def test_mipsel_malta_yamon(self):
        yamon_bin = 'yamon-02.22.bin'
        zip_path = self.ASSET_YAMON_ROM.fetch()
        with ZipFile(zip_path, 'r') as zf:
            zf.extract(yamon_bin, path=self.workdir)
        yamon_path = os.path.join(self.workdir, yamon_bin)

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
