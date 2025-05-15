#!/usr/bin/env python3
#
# Functional test for testing the q800 m68k machine
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset

class Q800MachineTest(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://snapshot.debian.org/'
         'archive/debian-ports/20191021T083923Z/pool-m68k/main/l/linux/'
         'kernel-image-5.3.0-1-m68k-di_5.3.7-1_m68k.udeb'),
        '949e50d74d4b9bc15d26c06d402717b7a4c0e32ff8100014f5930d8024de7b73')

    def test_m68k_q800(self):
        self.set_machine('q800')

        kernel_path = self.archive_extract(self.ASSET_KERNEL,
                                           member='boot/vmlinux-5.3.0-1-m68k')

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 vga=off')
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line,
                         '-audio', 'none')
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
        console_pattern = 'No filesystem could mount root'
        self.wait_for_console_pattern(console_pattern)

if __name__ == '__main__':
    LinuxKernelTest.main()
