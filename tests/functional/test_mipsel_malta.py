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

from qemu_test import QemuSystemTest, Asset
from qemu_test import interrupt_interactive_console_until_pattern
from qemu_test import wait_for_console_pattern
from zipfile import ZipFile


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
