# Test the bFLT loader format
#
# Copyright (C) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import bz2
import subprocess

from avocado import skipUnless
from avocado_qemu import QemuUserTest
from avocado_qemu import has_cmd


class LoadBFLT(QemuUserTest):

    def extract_cpio(self, cpio_path):
        """
        Extracts a cpio archive into the test workdir

        :param cpio_path: path to the cpio archive
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        with bz2.open(cpio_path, 'rb') as archive_cpio:
            subprocess.run(['cpio', '-i'], input=archive_cpio.read(),
                           stderr=subprocess.DEVNULL)
        os.chdir(cwd)

    @skipUnless(*has_cmd('cpio'))
    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_stm32(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=linux_user
        :avocado: tags=quick
        """
        # See https://elinux.org/STM32#User_Space
        rootfs_url = ('https://elinux.org/images/5/51/'
                      'Stm32_mini_rootfs.cpio.bz2')
        rootfs_hash = '9f065e6ba40cce7411ba757f924f30fcc57951e6'
        rootfs_path_bz2 = self.fetch_asset(rootfs_url, asset_hash=rootfs_hash)
        busybox_path = os.path.join(self.workdir, "/bin/busybox")

        self.extract_cpio(rootfs_path_bz2)

        res = self.run(busybox_path)
        ver = 'BusyBox v1.24.0.git (2015-02-03 22:17:13 CET) multi-call binary.'
        self.assertIn(ver, res.stdout_text)

        res = self.run(busybox_path, ['uname', '-a'])
        unm = 'armv7l GNU/Linux'
        self.assertIn(unm, res.stdout_text)
