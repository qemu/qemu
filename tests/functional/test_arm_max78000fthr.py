#!/usr/bin/env python3
#
# Functional test that checks the max78000fthr machine.
# Tests ICC, GCR, TRNG, AES, and UART
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class Max78000Machine(QemuSystemTest):

    ASSET_FW = Asset(
        'https://github.com/JacksonDonaldson/max78000Test/raw/main/build/max78000.bin',
        '86940b4bf60931bc6a8aa5db4b9f7f3cf8f64dbbd7ac534647980e536cf3adf7')

    def test_fthr(self):
        self.set_machine('max78000fthr')
        fw_path = self.ASSET_FW.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', fw_path)
        self.vm.add_args('-device', "loader,file=" + fw_path + ",addr=0x10000000")
        self.vm.launch()

        wait_for_console_pattern(self, 'started')

        # i -> prints instruction cache values
        exec_command_and_wait_for_pattern(self, 'i', 'CTRL: 00010001')

        # r -> gcr resets the machine
        exec_command_and_wait_for_pattern(self, 'r', 'started')

        # z -> sets some memory, then has gcr zero it
        exec_command_and_wait_for_pattern(self, 'z', 'initial value: 12345678')
        wait_for_console_pattern(self, "after memz: 00000000")

        # t -> runs trng
        exec_command_and_wait_for_pattern(self, 't', 'random data:')

        # a -> runs aes
        exec_command_and_wait_for_pattern(self, 'a',
                'encrypted to : a47ca9dd e0df4c86 a070af6e 91710dec')
        wait_for_console_pattern(self,
                'encrypted to : cab7a28e bf456751 9049fcea 8960494b')

if __name__ == '__main__':
    QemuSystemTest.main()
