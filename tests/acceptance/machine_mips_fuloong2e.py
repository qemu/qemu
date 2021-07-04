# Functional tests for the Lemote Fuloong-2E machine.
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern

class MipsFuloong2e(Test):

    timeout = 60

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    @skipUnless(os.getenv('RESCUE_YL_PATH'), 'RESCUE_YL_PATH not available')
    def test_linux_kernel_isa_serial(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:fuloong2e
        :avocado: tags=endian:little
        :avocado: tags=device:bonito64
        :avocado: tags=device:via686b
        """
        # Recovery system for the Yeeloong laptop
        # (enough to test the fuloong2e southbridge, accessing its ISA bus)
        # http://dev.lemote.com/files/resource/download/rescue/rescue-yl
        kernel_hash = 'ec4d1bd89a8439c41033ca63db60160cc6d6f09a'
        kernel_path = self.fetch_asset('file://' + os.getenv('RESCUE_YL_PATH'),
                                       asset_hash=kernel_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'Linux version 2.6.27.7lemote')
        cpu_revision = 'CPU revision is: 00006302 (ICT Loongson-2)'
        wait_for_console_pattern(self, cpu_revision)
