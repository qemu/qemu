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

class PowernvMachine(LinuxKernelTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    ASSET_KERNEL = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_powernv8-2025.02/vmlinux'),
        '6fd29aff9ad4362511ea5d0acbb510667c7031928e97d64ec15bbc5daf4b8151')

    ASSET_INITRD = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_powernv8-2025.02/rootfs.ext2'),
        'aee2192b692077c4bde31cb56ce474424b358f17cec323d5c94af3970c9aada2')

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

    def do_test_ppc64_powernv(self, proc):
        self.require_accelerator("tcg")
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-drive',
                         f'file={initrd_path},format=raw,if=none,id=drive0,readonly=on',
                         '-append', 'root=/dev/nvme0n1 console=tty0 console=hvc0',
                         '-device', 'pcie-pci-bridge,id=bridge1,bus=pcie.1,addr=0x0',
                         '-device', 'nvme,drive=drive0,bus=pcie.2,addr=0x0,serial=1234',
                         '-device', 'e1000e,bus=bridge1,addr=0x3',
                         '-device', 'nec-usb-xhci,bus=bridge1,addr=0x2')
        self.vm.launch()

        self.wait_for_console_pattern("CPU: " + proc + " generation processor")
        self.wait_for_console_pattern("INIT: Starting kernel at ")
        self.wait_for_console_pattern("Run /sbin/init as init process")
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

    def test_powernv11(self):
        self.set_machine('powernv11')
        self.do_test_ppc64_powernv('Power11')

if __name__ == '__main__':
    LinuxKernelTest.main()
