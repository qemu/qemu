#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern, skipFlakyTest


class RxGdbSimMachine(QemuSystemTest):

    timeout = 30
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    ASSET_UBOOT = Asset(
        ('https://github.com/philmd/qemu-testing-blob/raw/rx-gdbsim/rx/gdbsim/'
         'u-boot.bin'),
        'dd7dd4220cccf7aeb32227b26233bf39600db05c3f8e26005bcc2bf6c927207d')
    ASSET_DTB = Asset(
        ('https://github.com/philmd/qemu-testing-blob/raw/rx-gdbsim/rx/gdbsim/'
         'rx-gdbsim.dtb'),
        'aa278d9c1907a4501741d7ee57e7f65c02dd1b3e0323b33c6d4247f1b32cf29a')
    ASSET_KERNEL = Asset(
        ('https://github.com/philmd/qemu-testing-blob/raw/rx-gdbsim/rx/gdbsim/'
         'zImage'),
        'baa43205e74a7220ed8482188c5e9ce497226712abb7f4e7e4f825ce19ff9656')

    def test_uboot(self):
        """
        U-Boot and checks that the console is operational.
        """
        self.set_machine('gdbsim-r5f562n8')

        uboot_path = self.ASSET_UBOOT.fetch()

        self.vm.set_console()
        self.vm.add_args('-bios', uboot_path,
                         '-no-reboot')
        self.vm.launch()
        uboot_version = 'U-Boot 2016.05-rc3-23705-ga1ef3c71cb-dirty'
        wait_for_console_pattern(self, uboot_version)
        gcc_version = 'rx-unknown-linux-gcc (GCC) 9.0.0 20181105 (experimental)'
        # FIXME limit baudrate on chardev, else we type too fast
        #  https://gitlab.com/qemu-project/qemu/-/issues/2691
        #exec_command_and_wait_for_pattern(self, 'version', gcc_version)

    @skipFlakyTest(bug_url="https://gitlab.com/qemu-project/qemu/-/issues/2691")
    def test_linux_sash(self):
        """
        Boots a Linux kernel and checks that the console is operational.
        """
        self.set_machine('gdbsim-r5f562n7')

        dtb_path = self.ASSET_DTB.fetch()
        kernel_path = self.ASSET_KERNEL.fetch()

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'earlycon'
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-no-reboot')
        self.vm.launch()
        wait_for_console_pattern(self, 'Sash command shell (version 1.1.1)',
                                 failure_message='Kernel panic - not syncing')
        exec_command_and_wait_for_pattern(self, 'printenv', 'TERM=linux')

if __name__ == '__main__':
    QemuSystemTest.main()
