#!/usr/bin/env python3
#
# Functional test that boots a microblaze Linux kernel and checks the console
#
# Copyright (c) 2018, 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class MicroblazeMachine(QemuSystemTest):

    timeout = 90

    ASSET_IMAGE_BE = Asset(
        ('https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/'
         'day17.tar.xz'),
        '3ba7439dfbea7af4876662c97f8e1f0cdad9231fc166e4861d17042489270057')

    ASSET_IMAGE_LE = Asset(
        ('http://www.qemu-advent-calendar.org/2023/download/day13.tar.gz'),
        'b9b3d43c5dd79db88ada495cc6e0d1f591153fe41355e925d791fbf44de50c22')

    def do_ballerina_be_test(self, force_endianness=False):
        self.set_machine('petalogix-s3adsp1800')
        self.archive_extract(self.ASSET_IMAGE_BE)
        self.vm.set_console()
        self.vm.add_args('-kernel',
                         self.scratch_file('day17', 'ballerina.bin'))
        if force_endianness:
            self.vm.add_args('-M', 'endianness=big')
        self.vm.launch()
        wait_for_console_pattern(self, 'This architecture does not have '
                                       'kernel memory protection')
        # Note:
        # The kernel sometimes gets stuck after the "This architecture ..."
        # message, that's why we don't test for a later string here. This
        # needs some investigation by a microblaze wizard one day...

    def do_xmaton_le_test(self, force_endianness=False):
        self.require_netdev('user')
        self.set_machine('petalogix-s3adsp1800')
        self.archive_extract(self.ASSET_IMAGE_LE)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.scratch_file('day13', 'xmaton.bin'))
        if force_endianness:
            self.vm.add_args('-M', 'endianness=little')
        tftproot = self.scratch_file('day13')
        self.vm.add_args('-nic', f'user,tftp={tftproot}')
        self.vm.launch()
        wait_for_console_pattern(self, 'QEMU Advent Calendar 2023')
        wait_for_console_pattern(self, 'buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '#')
        exec_command_and_wait_for_pattern(self,
                'tftp -g -r xmaton.png 10.0.2.2 ; md5sum xmaton.png',
                '821cd3cab8efd16ad6ee5acc3642a8ea')


class MicroblazeBigEndianMachine(MicroblazeMachine):

    ASSET_IMAGE_BE = MicroblazeMachine.ASSET_IMAGE_BE
    ASSET_IMAGE_LE = MicroblazeMachine.ASSET_IMAGE_LE

    def test_microblaze_s3adsp1800_legacy_be(self):
        self.do_ballerina_be_test()

    def test_microblaze_s3adsp1800_legacy_le(self):
        self.do_xmaton_le_test(force_endianness=True)


if __name__ == '__main__':
    QemuSystemTest.main()
