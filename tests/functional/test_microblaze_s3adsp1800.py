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

class MicroblazeMachine(QemuSystemTest):

    timeout = 90

    ASSET_IMAGE = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day17.tar.xz'),
        '3ba7439dfbea7af4876662c97f8e1f0cdad9231fc166e4861d17042489270057')

    def test_microblaze_s3adsp1800(self):
        self.set_machine('petalogix-s3adsp1800')
        file_path = self.ASSET_IMAGE.fetch()
        archive_extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/day17/ballerina.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'This architecture does not have '
                                       'kernel memory protection')
        # Note:
        # The kernel sometimes gets stuck after the "This architecture ..."
        # message, that's why we don't test for a later string here. This
        # needs some investigation by a microblaze wizard one day...

if __name__ == '__main__':
    QemuSystemTest.main()
