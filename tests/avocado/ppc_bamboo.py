# Test that Linux kernel boots on the ppc bamboo board and check the console
#
# Copyright (c) 2021 Red Hat
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command_and_wait_for_pattern

class BambooMachine(QemuSystemTest):

    timeout = 90

    def test_ppc_bamboo(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:bamboo
        :avocado: tags=cpu:440epb
        :avocado: tags=device:rtl8139
        """
        tar_url = ('http://landley.net/aboriginal/downloads/binaries/'
                   'system-image-powerpc-440fp.tar.gz')
        tar_hash = '53e5f16414b195b82d2c70272f81c2eedb39bad9'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir +
                                   '/system-image-powerpc-440fp/linux',
                         '-initrd', self.workdir +
                                   '/system-image-powerpc-440fp/rootfs.cpio.gz',
                         '-nic', 'user,model=rtl8139,restrict=on')
        self.vm.launch()
        wait_for_console_pattern(self, 'Type exit when done')
        exec_command_and_wait_for_pattern(self, 'ping 10.0.2.2',
                                          '10.0.2.2 is alive!')
        exec_command_and_wait_for_pattern(self, 'halt', 'System Halted')
