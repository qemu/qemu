#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset

class XlnxVersalVirtMachine(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('http://ports.ubuntu.com/ubuntu-ports/dists/bionic-updates/main/'
         'installer-arm64/20101020ubuntu543.19/images/netboot/'
         'ubuntu-installer/arm64/linux'),
        'ce54f74ab0b15cfd13d1a293f2d27ffd79d8a85b7bb9bf21093ae9513864ac79')

    ASSET_INITRD = Asset(
        ('http://ports.ubuntu.com/ubuntu-ports/dists/bionic-updates/main/'
         'installer-arm64/20101020ubuntu543.19/images/netboot/'
         '/ubuntu-installer/arm64/initrd.gz'),
        'e7a5e716b6f516d8be315c06e7331aaf16994fe4222e0e7cfb34bc015698929e')

    def test_aarch64_xlnx_versal_virt(self):
        self.set_machine('xlnx-versal-virt')
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()

        self.vm.set_console()
        self.vm.add_args('-m', '2G',
                         '-accel', 'tcg',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path)
        self.vm.launch()
        self.wait_for_console_pattern('Checked W+X mappings: passed')

if __name__ == '__main__':
    LinuxKernelTest.main()
