#!/usr/bin/env python3
#
# Test that Linux kernel boots on the ppc bamboo board and check the console
#
# Copyright (c) 2021 Red Hat
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern


class BambooMachine(QemuSystemTest):

    timeout = 90

    ASSET_KERNEL = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc_bamboo-2023.11-8-gdcd9f0f6eb-20240105/vmlinux'),
        'a2e12eb45b73491ac62fc0bbeb68dead0dc5c0f22cf83146558389209b420ad1')
    ASSET_INITRD = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc_bamboo-2023.11-8-gdcd9f0f6eb-20240105/rootfs.cpio'),
        'd2a36bdb8763b389765dc8c29d4904cec2bd001c587f92e85ab9eb10d5ddda54')

    def test_ppc_bamboo(self):
        self.set_machine('bamboo')
        self.require_accelerator("tcg")
        self.require_netdev('user')

        kernel = self.ASSET_KERNEL.fetch()
        initrd = self.ASSET_INITRD.fetch()

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel,
                         '-initrd', initrd,
                         '-nic', 'user,model=virtio-net-pci,restrict=on')
        self.vm.launch()
        wait_for_console_pattern(self, 'buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')
        exec_command_and_wait_for_pattern(self, 'ping -c1 10.0.2.2',
                '1 packets transmitted, 1 packets received, 0% packet loss')
        exec_command_and_wait_for_pattern(self, 'halt', 'System Halted')

if __name__ == '__main__':
    QemuSystemTest.main()
