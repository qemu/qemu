#!/usr/bin/env python3
#
# Functional test that boots a sam460ex machine with a PPC 460EX CPU
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class sam460exTest(LinuxKernelTest):

    ASSET_BR2_SAM460EX_LINUX = Asset(
        'https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/buildroot/qemu_ppc_sam460ex-2023.11-8-gdcd9f0f6eb-20240105/vmlinux',
        '6f46346f3e20e8b5fc050ff363f350f8b9d76a051b9e0bd7ea470cc680c14df2')

    def test_ppc_sam460ex_buildroot(self):
        self.set_machine('sam460ex')
        self.require_netdev('user')

        linux_path = self.ASSET_BR2_SAM460EX_LINUX.fetch()

        self.vm.set_console()
        self.vm.add_args('-kernel', linux_path,
                         '-device', 'virtio-net-pci,netdev=net0',
                         '-netdev', 'user,id=net0')
        self.vm.launch()

        self.wait_for_console_pattern('Linux version')
        self.wait_for_console_pattern('Hardware name: amcc,canyonlands 460EX')
        self.wait_for_console_pattern('/init as init process')
        self.wait_for_console_pattern('lease of 10.0.2.15 obtained')
        self.wait_for_console_pattern('buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')
        exec_command_and_wait_for_pattern(self, 'poweroff', 'System Halted')

if __name__ == '__main__':
    LinuxKernelTest.main()
