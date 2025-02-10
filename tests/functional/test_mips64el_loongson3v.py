#!/usr/bin/env python3
#
# Functional tests for the Generic Loongson-3 Platform.
#
# Copyright (c) 2021 Jiaxun Yang <jiaxun.yang@flygoat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern, skipUntrustedTest


class MipsLoongson3v(QemuSystemTest):
    timeout = 60

    ASSET_PMON = Asset(
        ('https://github.com/loongson-community/pmon/'
         'releases/download/20210112/pmon-3avirt.bin'),
        'fcdf6bb2cb7885a4a62f31fcb0d5e368bac7b6cea28f40c6dfa678af22fea20a')

    @skipUntrustedTest()
    def test_pmon_serial_console(self):
        self.set_machine('loongson3-virt')

        pmon_path = self.ASSET_PMON.fetch()

        self.vm.set_console()
        self.vm.add_args('-bios', pmon_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'CPU GODSON3 BogoMIPS:')

if __name__ == '__main__':
    QemuSystemTest.main()
