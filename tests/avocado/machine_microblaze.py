# Functional test that boots a microblaze Linux kernel and checks the console
#
# Copyright (c) 2018, 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import time
from avocado_qemu import exec_command, exec_command_and_wait_for_pattern
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive

class MicroblazeMachine(QemuSystemTest):

    timeout = 90

    def test_microblaze_s3adsp1800(self):
        """
        :avocado: tags=arch:microblaze
        :avocado: tags=machine:petalogix-s3adsp1800
        """

        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day17.tar.xz')
        tar_hash = '08bf3e3bfb6b6c7ce1e54ab65d54e189f2caf13f'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console()
        self.vm.add_args('-kernel', self.workdir + '/day17/ballerina.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'This architecture does not have '
                                       'kernel memory protection')
        # Note:
        # The kernel sometimes gets stuck after the "This architecture ..."
        # message, that's why we don't test for a later string here. This
        # needs some investigation by a microblaze wizard one day...

    def test_microblazeel_s3adsp1800(self):
        """
        :avocado: tags=arch:microblazeel
        :avocado: tags=machine:petalogix-s3adsp1800
        """

        self.require_netdev('user')
        tar_url = ('http://www.qemu-advent-calendar.org/2023/download/'
                   'day13.tar.gz')
        tar_hash = '6623d5fff5f84cfa8f34e286f32eff6a26546f44'
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
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
