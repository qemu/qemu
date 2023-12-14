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
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day04.tar.xz')
        tar_hash = 'f46724d281a9f30fa892d458be7beb7d34dc25f9'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/creek/creek.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU advent calendar 2020',
                                 self.panic_message)
