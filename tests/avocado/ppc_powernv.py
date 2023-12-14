# Test that Linux kernel boots on ppc powernv machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class powernvMachine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    def do_test_linux_boot(self):
        self.require_accelerator("tcg")
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

    def test_linux_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv
        :avocado: tags=accel:tcg
        """

        self.do_test_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_linux_smp_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv
        :avocado: tags=accel:tcg
        """

        self.vm.add_args('-smp', '4')
        self.do_test_linux_boot()
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_smt_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv
        :avocado: tags=accel:tcg
        """

        self.vm.add_args('-smp', '4,threads=4')
        self.do_test_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_big_boot(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv
        :avocado: tags=accel:tcg
        """

        self.vm.add_args('-smp', '16,threads=4,cores=2,sockets=2')

        # powernv does not support NUMA
        self.do_test_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 2 nodes, 16 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)
