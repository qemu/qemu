# Functional test that boots a microblaze Linux kernel and checks the console
#
# Copyright (c) 2018, 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

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
