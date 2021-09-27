# Test that Linux kernel boots on ppc machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class Mpc8544dsMachine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    panic_message = 'Kernel panic - not syncing'

    def test_ppc_mpc8544ds(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:mpc8544ds
        """
        tar_url = ('https://www.qemu-advent-calendar.org'
                   '/2020/download/day17.tar.gz')
        tar_hash = '7a5239542a7c4257aa4d3b7f6ddf08fb6775c494'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/creek/creek.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU advent calendar 2020',
                                 self.panic_message)
