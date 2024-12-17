#!/usr/bin/env python3
#
# Test that Linux kernel boots on ppc machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class Mpc8544dsMachine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    panic_message = 'Kernel panic - not syncing'

    ASSET_IMAGE = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day04.tar.xz'),
        '88bc83f3c9f3d633bcfc108a6342d677abca247066a2fb8d4636744a0d319f94')

    def test_ppc_mpc8544ds(self):
        self.require_accelerator("tcg")
        self.set_machine('mpc8544ds')
        kernel_file = self.archive_extract(self.ASSET_IMAGE,
                                           member='creek/creek.bin')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file)
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU advent calendar 2020',
                                 self.panic_message)

if __name__ == '__main__':
    QemuSystemTest.main()
