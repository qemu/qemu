# Functional tests for the MIPS Malta board
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern


class MaltaMachine(QemuSystemTest):

    def do_test_yamon(self):
        rom_url = ('https://s3-eu-west-1.amazonaws.com/'
                   'downloads-mips/mips-downloads/'
                   'YAMON/yamon-bin-02.22.zip')
        rom_hash = '8da7ecddbc5312704b8b324341ee238189bde480'
        zip_path = self.fetch_asset(rom_url, asset_hash=rom_hash)

        archive.extract(zip_path, self.workdir)
        yamon_path = os.path.join(self.workdir, 'yamon-02.22.bin')

        self.vm.set_console()
        self.vm.add_args('-bios', yamon_path)
        self.vm.launch()

        prompt =  'YAMON>'
        pattern = 'YAMON ROM Monitor'
        interrupt_interactive_console_until_pattern(self, pattern, prompt)
        wait_for_console_pattern(self, prompt)
        self.vm.shutdown()

    def test_mipsel_malta_yamon(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        """
        self.do_test_yamon()

    def test_mips64el_malta_yamon(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        """
        self.do_test_yamon()
