#!/usr/bin/env python3
#
# Functional tests for the Lemote Fuloong-2E machine.
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import subprocess

from qemu_test import LinuxKernelTest, Asset
from qemu_test import wait_for_console_pattern, skipUntrustedTest
from unittest import skipUnless

class MipsFuloong2e(LinuxKernelTest):

    timeout = 60

    ASSET_KERNEL = Asset(
        ('http://archive.debian.org/debian/pool/main/l/linux/'
         'linux-image-3.16.0-6-loongson-2e_3.16.56-1+deb8u1_mipsel.deb'),
        '2a70f15b397f4ced632b0c15cb22660394190644146d804d60a4796eefbe1f50')

    def test_linux_kernel_3_16(self):
        kernel_path = self.archive_extract(
            self.ASSET_KERNEL,
            member='boot/vmlinux-3.16.0-6-loongson-2e')

        self.set_machine('fuloong2e')
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    @skipUntrustedTest()
    @skipUnless(os.getenv('RESCUE_YL_PATH'), 'RESCUE_YL_PATH not available')
    def test_linux_kernel_2_6_27_isa_serial(self):
        # Recovery system for the Yeeloong laptop
        # (enough to test the fuloong2e southbridge, accessing its ISA bus)
        # http://dev.lemote.com/files/resource/download/rescue/rescue-yl
        sha = 'ab588d3316777c62cc81baa20ac92e98b01955c244dff3794b711bc34e26e51d'
        kernel_path = os.getenv('RESCUE_YL_PATH')
        output = subprocess.check_output(['sha256sum', kernel_path])
        checksum = output.split()[0]
        assert checksum.decode("utf-8") == sha

        self.set_machine('fuloong2e')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'Linux version 2.6.27.7lemote')
        cpu_revision = 'CPU revision is: 00006302 (ICT Loongson-2)'
        wait_for_console_pattern(self, cpu_revision)


if __name__ == '__main__':
    LinuxKernelTest.main()
