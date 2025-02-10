#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# LoongArch virt test.
#
# Copyright (c) 2023 Loongson Technology Corporation Limited
#

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern

class LoongArchMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    timeout = 120

    ASSET_KERNEL = Asset(
        ('https://github.com/yangxiaojuan-loongson/qemu-binary/'
         'releases/download/2024-11-26/vmlinuz.efi'),
        '08b88a45f48a5fd92260bae895be4e5175be2397481a6f7821b9f39b2965b79e')
    ASSET_INITRD = Asset(
        ('https://github.com/yangxiaojuan-loongson/qemu-binary/'
         'releases/download/2024-11-26/ramdisk'),
        '03d6fb6f8ee64ecac961120a0bdacf741f17b3bee2141f17fa01908c8baf176a')
    ASSET_BIOS = Asset(
        ('https://github.com/yangxiaojuan-loongson/qemu-binary/'
         'releases/download/2024-11-26/QEMU_EFI.fd'),
        'f55fbf5d92e885844631ae9bfa8887f659bbb4f6ef2beea9e9ff8bc0603b6697')

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def test_loongarch64_devices(self):

        self.set_machine('virt')

        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        bios_path = self.ASSET_BIOS.fetch()

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'root=/dev/ram rdinit=/sbin/init console=ttyS0,115200')
        self.vm.add_args('-nographic',
                         '-smp', '4',
                         '-m', '1024',
                         '-cpu', 'la464',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-bios', bios_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        self.wait_for_console_pattern('Run /sbin/init as init process')
        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                          'processor		: 3')

if __name__ == '__main__':
    QemuSystemTest.main()
