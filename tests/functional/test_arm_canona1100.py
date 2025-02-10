#!/usr/bin/env python3
#
# Functional test that boots the canon-a1100 machine with firmware
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class CanonA1100Machine(QemuSystemTest):
    """Boots the barebox firmware and checks that the console is operational"""

    timeout = 90

    ASSET_BIOS = Asset(('https://qemu-advcal.gitlab.io'
                        '/qac-best-of-multiarch/download/day18.tar.xz'),
                       '28e71874ce985be66b7fd1345ed88cb2523b982f899c8d2900d6353054a1be49')

    def test_arm_canona1100(self):
        self.set_machine('canon-a1100')

        bios = self.archive_extract(self.ASSET_BIOS,
                                    member="day18/barebox.canon-a1100.bin")
        self.vm.set_console()
        self.vm.add_args('-bios', bios)
        self.vm.launch()
        wait_for_console_pattern(self, 'running /env/bin/init')

if __name__ == '__main__':
    QemuSystemTest.main()
