# Test that Linux kernel boots on ppc machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class VirtexMl507Machine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    panic_message = 'Kernel panic - not syncing'

    def test_ppc_virtex_ml507(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:virtex-ml507
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day08.tar.xz')
        tar_hash = '74c68f5af7a7b8f21c03097b298f3bb77ff52c1f'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/hippo/hippo.linux',
                         '-dtb', self.workdir + '/hippo/virtex440-ml507.dtb',
                         '-m', '512')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU advent calendar 2020',
                                 self.panic_message)
