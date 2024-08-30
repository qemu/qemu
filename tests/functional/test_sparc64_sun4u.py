#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import os

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test.utils import archive_extract

class Sun4uMachine(QemuSystemTest):
    """Boots the Linux kernel and checks that the console is operational"""

    timeout = 90

    ASSET_IMAGE = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day23.tar.xz'),
        'a3ed92450704af244178351afd0e769776e7decb298e95a63abfd9a6e3f6c854')

    def test_sparc64_sun4u(self):
        self.set_machine('sun4u')
        file_path = self.ASSET_IMAGE.fetch()
        kernel_name = 'day23/vmlinux'
        archive_extract(file_path, self.workdir, kernel_name)
        self.vm.set_console()
        self.vm.add_args('-kernel', os.path.join(self.workdir, kernel_name),
                         '-append', 'printk.time=0')
        self.vm.launch()
        wait_for_console_pattern(self, 'Starting logging: OK')

if __name__ == '__main__':
    QemuSystemTest.main()
