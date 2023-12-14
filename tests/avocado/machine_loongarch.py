# SPDX-License-Identifier: GPL-2.0-or-later
#
# LoongArch virt test.
#
# Copyright (c) 2023 Loongson Technology Corporation Limited
#

from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern

class LoongArchMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    timeout = 120

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def test_loongarch64_devices(self):

        """
        :avocado: tags=arch:loongarch64
        :avocado: tags=machine:virt
        """

        kernel_url = ('https://github.com/yangxiaojuan-loongson/qemu-binary/'
                      'releases/download/binary-files/vmlinuz.efi')
        kernel_hash = '951b485b16e3788b6db03a3e1793c067009e31a2'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = ('https://github.com/yangxiaojuan-loongson/qemu-binary/'
                      'releases/download/binary-files/ramdisk')
        initrd_hash = 'c67658d9b2a447ce7db2f73ba3d373c9b2b90ab2'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        bios_url = ('https://github.com/yangxiaojuan-loongson/qemu-binary/'
                    'releases/download/binary-files/QEMU_EFI.fd')
        bios_hash = ('dfc1bfba4853cd763b9d392d0031827e8addbca8')
        bios_path = self.fetch_asset(bios_url, asset_hash=bios_hash)

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
