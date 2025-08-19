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


class VirtexMl507Machine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    panic_message = 'Kernel panic - not syncing'

    ASSET_IMAGE = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day08.tar.xz'),
        'cefe5b8aeb5e9d2d1d4fd22dcf48d917d68d5a765132bf2ddd6332dc393b824c')

    def test_ppc_virtex_ml507(self):
        self.require_accelerator("tcg")
        self.set_machine('virtex-ml507')
        self.archive_extract(self.ASSET_IMAGE)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.scratch_file('hippo', 'hippo.linux'),
                         '-dtb', self.scratch_file('hippo',
                                                   'virtex440-ml507.dtb'),
                         '-m', '512')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU advent calendar 2020',
                                 self.panic_message)

if __name__ == '__main__':
    QemuSystemTest.main()
