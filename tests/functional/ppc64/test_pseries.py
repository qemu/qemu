#!/usr/bin/env python3
#
# Test that Linux kernel boots on ppc machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from test_migration import PpcMigrationTest

class PseriesMachine(QemuSystemTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora-secondary/'
         'releases/29/Everything/ppc64le/os/ppc/ppc64/vmlinuz'),
        '383c2f5c23bc0d9d32680c3924d3fd7ee25cc5ef97091ac1aa5e1d853422fc5f')

    def do_test_ppc64_linux_boot(self, kernel_command_line = KERNEL_COMMON_COMMAND_LINE):
        kernel_path = self.ASSET_KERNEL.fetch()

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()

    def test_ppc64_vof_linux_boot(self):
        self.set_machine('pseries')
        self.vm.add_args('-machine', 'x-vof=on')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_ppc64_linux_boot(self):
        self.set_machine('pseries')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_ppc64_linux_smp_boot(self):
        self.set_machine('pseries')
        self.vm.add_args('-smp', '4')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_ppc64_linux_hpt_smp_boot(self):
        self.set_machine('pseries')
        self.vm.add_args('-smp', '4')
        self.do_test_ppc64_linux_boot(self.KERNEL_COMMON_COMMAND_LINE +
                                      'disable_radix')
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, 'hash-mmu: Initializing hash mmu',
                                 self.panic_message)
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_ppc64_linux_smt_boot(self):
        self.set_machine('pseries')
        self.vm.add_args('-smp', '4,threads=4')
        self.do_test_ppc64_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_ppc64_linux_big_boot(self):
        self.set_machine('pseries')
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

    def test_ppc64_linux_migration(self):
        self.set_machine('pseries')

        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE

        dest_vm = self.get_vm(name="dest-qemu")
        dest_vm.add_args('-incoming', 'defer')
        dest_vm.add_args('-smp', '4')
        dest_vm.add_args('-nodefaults')
        dest_vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        dest_vm.set_console()
        dest_vm.launch()

        source_vm = self.get_vm(name="source-qemu")
        source_vm.add_args('-smp', '4')
        source_vm.add_args('-nodefaults')
        source_vm.add_args('-kernel', kernel_path,
                           '-append', kernel_command_line)
        source_vm.set_console()
        source_vm.launch()

        # ensure the boot has reached Linux
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message,
                                 vm=source_vm)

        PpcMigrationTest().do_migrate_ppc64_linux(source_vm, dest_vm);

        # ensure the boot proceeds after migration
        wait_for_console_pattern(self, self.good_message, self.panic_message,
                                 vm=dest_vm)

if __name__ == '__main__':
    QemuSystemTest.main()
