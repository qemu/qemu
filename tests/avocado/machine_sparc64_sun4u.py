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

from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive
from boot_linux_console import LinuxKernelTest

class Sun4uMachine(LinuxKernelTest):
    """Boots the Linux kernel and checks that the console is operational"""

    timeout = 90

    def test_sparc64_sun4u(self):
        """
        :avocado: tags=arch:sparc64
        :avocado: tags=machine:sun4u
        """
        tar_url = ('https://www.qemu-advent-calendar.org'
                   '/2018/download/day23.tar.xz')
        tar_hash = '142db83cd974ffadc4f75c8a5cad5bcc5722c240'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/day23/vmlinux',
                         '-append', self.KERNEL_COMMON_COMMAND_LINE)
        self.vm.launch()
        wait_for_console_pattern(self, 'Starting logging: OK')
