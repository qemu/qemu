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

from qemu_test import QemuSystemTest
from qemu_test import wait_for_console_pattern
from unittest import skipUnless

class MipsFuloong2e(QemuSystemTest):

    timeout = 60

    @skipUnless(os.getenv('QEMU_TEST_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
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
    QemuSystemTest.main()
