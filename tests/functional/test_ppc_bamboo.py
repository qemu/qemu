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

    ASSET_IMAGE = Asset(
        ('http://landley.net/aboriginal/downloads/binaries/'
         'system-image-powerpc-440fp.tar.gz'),
        'c12b58f841c775a0e6df4832a55afe6b74814d1565d08ddeafc1fb949a075c5e')

    def test_ppc_bamboo(self):
        self.set_machine('bamboo')
        self.require_accelerator("tcg")
        self.require_netdev('user')
        self.archive_extract(self.ASSET_IMAGE)
        self.vm.set_console()
        self.vm.add_args('-kernel',
                         self.scratch_file('system-image-powerpc-440fp',
                                           'linux'),
                         '-initrd',
                         self.scratch_file('system-image-powerpc-440fp',
                                           'rootfs.cpio.gz'),
                         '-nic', 'user,model=rtl8139,restrict=on')
        self.vm.launch()
        wait_for_console_pattern(self, 'Type exit when done')
        exec_command_and_wait_for_pattern(self, 'ping 10.0.2.2',
                                          '10.0.2.2 is alive!')
        exec_command_and_wait_for_pattern(self, 'halt', 'System Halted')

if __name__ == '__main__':
    QemuSystemTest.main()
