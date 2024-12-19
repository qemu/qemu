#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on an Alpha Clipper machine
# and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class AlphaClipperTest(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('http://archive.debian.org/debian/dists/lenny/main/'
         'installer-alpha/20090123lenny10/images/cdrom/vmlinuz'),
        '34f53da3fa32212e4f00b03cb944b2ad81c06bc8faaf9b7193b2e544ceeca576')

    def test_alpha_clipper(self):
        self.set_machine('clipper')
        kernel_path = self.ASSET_KERNEL.fetch()

        uncompressed_kernel = self.uncompress(self.ASSET_KERNEL, format="gz")

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-nodefaults',
                         '-kernel', uncompressed_kernel,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

if __name__ == '__main__':
    LinuxKernelTest.main()
