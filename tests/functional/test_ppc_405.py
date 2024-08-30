#!/usr/bin/env python3
#
# Test that the U-Boot firmware boots on ppc 405 machines and check the console
#
# Copyright (c) 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern

class Ppc405Machine(QemuSystemTest):

    timeout = 90

    ASSET_UBOOT = Asset(
        ('https://gitlab.com/huth/u-boot/-/raw/taihu-2021-10-09/'
         'u-boot-taihu.bin'),
        'a076bb6cdeaafa406330e51e074b66d8878d9036d67d4caa0137be03ee4c112c')

    def do_test_ppc405(self):
        file_path = self.ASSET_UBOOT.fetch()
        self.vm.set_console(console_index=1)
        self.vm.add_args('-bios', file_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'AMCC PPC405EP Evaluation Board')
        exec_command_and_wait_for_pattern(self, 'reset', 'AMCC PowerPC 405EP')

    def test_ppc_ref405ep(self):
        self.require_accelerator("tcg")
        self.set_machine('ref405ep')
        self.do_test_ppc405()

if __name__ == '__main__':
    QemuSystemTest.main()
