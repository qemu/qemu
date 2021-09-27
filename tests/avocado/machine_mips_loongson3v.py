# Functional tests for the Generic Loongson-3 Platform.
#
# Copyright (c) 2021 Jiaxun Yang <jiaxun.yang@flygoat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import time

from avocado import skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class MipsLoongson3v(QemuSystemTest):
    timeout = 60

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_pmon_serial_console(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=endian:little
        :avocado: tags=machine:loongson3-virt
        :avocado: tags=cpu:Loongson-3A1000
        :avocado: tags=device:liointc
        :avocado: tags=device:goldfish_rtc
        """

        pmon_hash = '7c8b45dd81ccfc55ff28f5aa267a41c3'
        pmon_path = self.fetch_asset('https://github.com/loongson-community/pmon/'
                                    'releases/download/20210112/pmon-3avirt.bin',
                                     asset_hash=pmon_hash, algorithm='md5')

        self.vm.set_console()
        self.vm.add_args('-bios', pmon_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'CPU GODSON3 BogoMIPS:')
