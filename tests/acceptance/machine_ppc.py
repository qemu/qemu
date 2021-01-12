# Test that Linux kernel boots on ppc machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern

class PpcMachine(Test):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    panic_message = 'Kernel panic - not syncing'

    def test_ppc64_pseries(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive'
                      '/fedora-secondary/releases/29/Everything/ppc64le/os'
                      '/ppc/ppc64/vmlinuz')
        kernel_hash = '3fe04abfc852b66653b8c3c897a59a689270bc77'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=hvc0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        wait_for_console_pattern(self, console_pattern, self.panic_message)

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

    def test_ppc_virtex_ml507(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:virtex-ml507
        """
        tar_url = ('https://www.qemu-advent-calendar.org'
                   '/2020/download/hippo.tar.gz')
        tar_hash = '306b95bfe7d147f125aa176a877e266db8ef914a'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/hippo/hippo.linux',
                         '-dtb', self.workdir + '/hippo/virtex440-ml507.dtb',
                         '-m', '512')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU advent calendar 2020',
                                 self.panic_message)
