#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import shutil

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern
from qemu_test.utils import file_truncate

class EmcraftSf2Machine(LinuxKernelTest):

    ASSET_UBOOT = Asset(
        ('https://raw.githubusercontent.com/Subbaraya-Sundeep/qemu-test-binaries/'
         'fe371d32e50ca682391e1e70ab98c2942aeffb01/u-boot'),
        '5c6a15103375db11b21f2236473679a9dbbed6d89652bfcdd501c263d68ab725')

    ASSET_SPI = Asset(
        ('https://raw.githubusercontent.com/Subbaraya-Sundeep/qemu-test-binaries/'
         'fe371d32e50ca682391e1e70ab98c2942aeffb01/spi.bin'),
        'cd9bdd2c4cb55a59c3adb6bcf74881667c4500dde0570a43aa3be2b17eecfdb6')

    def test_arm_emcraft_sf2(self):
        self.set_machine('emcraft-sf2')
        self.require_netdev('user')

        uboot_path = self.ASSET_UBOOT.fetch()
        spi_path = self.ASSET_SPI.fetch()
        spi_path_rw = self.scratch_file('spi.bin')
        shutil.copy(spi_path, spi_path_rw)
        os.chmod(spi_path_rw, 0o600)

        file_truncate(spi_path_rw, 16 << 20) # Spansion S25FL128SDPBHICO is 16 MiB

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        self.vm.add_args('-kernel', uboot_path,
                         '-append', kernel_command_line,
                         '-drive', 'file=' + spi_path_rw + ',if=mtd,format=raw',
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Enter \'help\' for a list')

        exec_command_and_wait_for_pattern(self, 'ifconfig eth0 10.0.2.15',
                                                 'eth0: link becomes ready')
        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2',
            '3 packets transmitted, 3 packets received, 0% packet loss')

if __name__ == '__main__':
    LinuxKernelTest.main()
