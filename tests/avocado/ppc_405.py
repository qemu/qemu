# Test that the U-Boot firmware boots on ppc 405 machines and check the console
#
# Copyright (c) 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command_and_wait_for_pattern

class Ppc405Machine(QemuSystemTest):

    timeout = 90

    def do_test_ppc405(self):
        uboot_url = ('https://gitlab.com/huth/u-boot/-/raw/'
                     'taihu-2021-10-09/u-boot-taihu.bin')
        uboot_hash = ('3208940e908a5edc7c03eab072c60f0dcfadc2ab');
        file_path = self.fetch_asset(uboot_url, asset_hash=uboot_hash)
        self.vm.set_console(console_index=1)
        self.vm.add_args('-bios', file_path)
        self.vm.launch()
        wait_for_console_pattern(self, 'AMCC PPC405EP Evaluation Board')
        exec_command_and_wait_for_pattern(self, 'reset', 'AMCC PowerPC 405EP')

    def test_ppc_ref405ep(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:ref405ep
        :avocado: tags=cpu:405ep
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.do_test_ppc405()
