#!/usr/bin/env python3
#
# Functional test that boots a microblaze Linux kernel and checks the console
#
# Copyright (c) 2018, 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import time
from qemu_test import exec_command, exec_command_and_wait_for_pattern
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test.utils import archive_extract

class MicroblazeelMachine(QemuSystemTest):

    timeout = 90

    ASSET_IMAGE = Asset(
        ('http://www.qemu-advent-calendar.org/2023/download/day13.tar.gz'),
        'b9b3d43c5dd79db88ada495cc6e0d1f591153fe41355e925d791fbf44de50c22')

    def test_microblazeel_s3adsp1800(self):
        self.require_netdev('user')
        self.set_machine('petalogix-s3adsp1800')
        file_path = self.ASSET_IMAGE.fetch()
        archive_extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/day13/xmaton.bin')
        self.vm.add_args('-nic', 'user,tftp=' + self.workdir + '/day13/')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU Advent Calendar 2023')
        time.sleep(0.1)
        exec_command(self, 'root')
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self,
                'tftp -g -r xmaton.png 10.0.2.2 ; md5sum xmaton.png',
                '821cd3cab8efd16ad6ee5acc3642a8ea')

if __name__ == '__main__':
    QemuSystemTest.main()
