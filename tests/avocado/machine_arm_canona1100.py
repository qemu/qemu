# Functional test that boots the canon-a1100 machine with firmware
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive

class CanonA1100Machine(QemuSystemTest):
    """Boots the barebox firmware and checks that the console is operational"""

    timeout = 90

    def test_arm_canona1100(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:canon-a1100
        :avocado: tags=device:pflash_cfi02
        """
        tar_url = ('https://www.qemu-advent-calendar.org'
                   '/2018/download/day18.tar.xz')
        tar_hash = '068b5fc4242b29381acee94713509f8a876e9db6'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-bios',
                         self.workdir + '/day18/barebox.canon-a1100.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'running /env/bin/init')
