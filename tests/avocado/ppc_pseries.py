# Test that Linux kernel boots on ppc machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class pseriesMachine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    def do_test_ppc64_linux_boot(self, kernel_command_line = KERNEL_COMMON_COMMAND_LINE):
        kernel_url = ('https://archives.fedoraproject.org/pub/archive'
                      '/fedora-secondary/releases/29/Everything/ppc64le/os'
                      '/ppc/ppc64/vmlinuz')
        kernel_hash = '3fe04abfc852b66653b8c3c897a59a689270bc77'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()

    def test_ppc64_vof_linux_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        self.vm.add_args('-machine', 'x-vof=on')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_ppc64_linux_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        self.do_test_ppc64_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_ppc64_linux_smp_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        self.vm.add_args('-smp', '4')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_ppc64_linux_hpt_smp_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        self.vm.add_args('-smp', '4')
        self.do_test_ppc64_linux_boot(self.KERNEL_COMMON_COMMAND_LINE +
                                      'disable_radix')
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, 'hash-mmu: Initializing hash mmu',
                                 self.panic_message)
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_ppc64_linux_smt_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        self.vm.add_args('-smp', '4,threads=4')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_ppc64_linux_big_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        """

        self.vm.add_args('-smp', '16,threads=4,cores=2,sockets=2')
        self.vm.add_args('-m', '512M',
                         '-object', 'memory-backend-ram,size=256M,id=m0',
                         '-object', 'memory-backend-ram,size=256M,id=m1')
        self.vm.add_args('-numa', 'node,nodeid=0,memdev=m0')
        self.vm.add_args('-numa', 'node,nodeid=1,memdev=m1')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 2 nodes, 16 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)
