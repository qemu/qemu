#!/usr/bin/env python3
#
# Test that Linux kernel boots on ppc powernv machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset
from qemu_test import wait_for_console_pattern

class powernvMachine(LinuxKernelTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora-secondary/'
         'releases/29/Everything/ppc64le/os/ppc/ppc64/vmlinuz'),
        '383c2f5c23bc0d9d32680c3924d3fd7ee25cc5ef97091ac1aa5e1d853422fc5f')

    def do_test_linux_boot(self, command_line = KERNEL_COMMON_COMMAND_LINE):
        self.require_accelerator("tcg")
        kernel_path = self.ASSET_KERNEL.fetch()

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-append', command_line)
        self.vm.launch()

    def test_linux_boot(self):
        self.set_machine('powernv')
        self.do_test_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_linux_smp_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '4')
        self.do_test_linux_boot()
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_smp_hpt_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '4')
        self.do_test_linux_boot(self.KERNEL_COMMON_COMMAND_LINE +
                                'disable_radix')
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, 'hash-mmu: Initializing hash mmu',
                                 self.panic_message)
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_smt_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '4,threads=4')
        self.do_test_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_big_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '16,threads=4,cores=2,sockets=2')

        # powernv does not support NUMA
        self.do_test_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 2 nodes, 16 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)


    ASSET_EPAPR_KERNEL = Asset(
        ('https://github.com/open-power/op-build/releases/download/v2.7/'
         'zImage.epapr'),
        '0ab237df661727e5392cee97460e8674057a883c5f74381a128fa772588d45cd')

    def do_test_ppc64_powernv(self, proc):
        self.require_accelerator("tcg")
        kernel_path = self.ASSET_EPAPR_KERNEL.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-append', 'console=tty0 console=hvc0',
                         '-device', 'pcie-pci-bridge,id=bridge1,bus=pcie.1,addr=0x0',
                         '-device', 'nvme,bus=pcie.2,addr=0x0,serial=1234',
                         '-device', 'e1000e,bus=bridge1,addr=0x3',
                         '-device', 'nec-usb-xhci,bus=bridge1,addr=0x2')
        self.vm.launch()

        self.wait_for_console_pattern("CPU: " + proc + " generation processor")
        self.wait_for_console_pattern("zImage starting: loaded")
        self.wait_for_console_pattern("Run /init as init process")
        # Device detection output driven by udev probing is sometimes cut off
        # from console output, suspect S14silence-console init script.

    def test_powernv8(self):
        self.set_machine('powernv8')
        self.do_test_ppc64_powernv('P8')

    def test_powernv9(self):
        self.set_machine('powernv9')
        self.do_test_ppc64_powernv('P9')

    def test_powernv10(self):
        self.set_machine('powernv10')
        self.do_test_ppc64_powernv('P10')

if __name__ == '__main__':
    LinuxKernelTest.main()
