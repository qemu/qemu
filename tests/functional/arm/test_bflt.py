#!/usr/bin/env python3
#
# Test the bFLT loader format
#
# Copyright (C) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import bz2

from qemu_test import QemuUserTest, Asset
from qemu_test import skipIfMissingCommands, skipUntrustedTest


class LoadBFLT(QemuUserTest):

    ASSET_ROOTFS = Asset(
        ('https://elinux.org/images/5/51/Stm32_mini_rootfs.cpio.bz2'),
         'eefb788e4980c9e8d6c9d60ce7d15d4da6bf4fbc6a80f487673824600d5ba9cc')

    @skipIfMissingCommands('cpio')
    @skipUntrustedTest()
    def test_stm32(self):
        # See https://elinux.org/STM32#User_Space
        rootfs_path_bz2 = self.ASSET_ROOTFS.fetch()
        busybox_path = self.scratch_file("bin", "busybox")

        with bz2.open(rootfs_path_bz2, 'rb') as cpio_handle:
            self.archive_extract(cpio_handle, format="cpio")

        res = self.run_cmd(busybox_path)
        ver = 'BusyBox v1.24.0.git (2015-02-03 22:17:13 CET) multi-call binary.'
        self.assertIn(ver, res.stdout)

        res = self.run_cmd(busybox_path, ['uname', '-a'])
        unm = 'armv7l GNU/Linux'
        self.assertIn(unm, res.stdout)


if __name__ == '__main__':
    QemuUserTest.main()
