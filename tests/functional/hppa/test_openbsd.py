#!/usr/bin/env python3
#
# Test that OpenBSD boots on B160L and reaches the installer.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class OpenBSDB160L(QemuSystemTest):

    ASSET_CD = Asset(
        'https://ftp.eu.openbsd.org/pub/OpenBSD/8.0/hppa/cd80.iso',
        '9b3971dc3d2362e5d694495f2c2e10795513b87c9bcca5666df5a2bfea071663')

    def test_openbsd_80_installer(self):
        self.set_machine('B160L')
        self.require_accelerator('tcg')
        cdrom_path = self.ASSET_CD.fetch()

        self.vm.set_console()
        self.vm.add_args('-nic', 'none',
                         '-cdrom', cdrom_path,
                         '-boot', 'd',
                         '-no-reboot')
        self.vm.launch()

        wait_for_console_pattern(self, 'OpenBSD 8.0 (RAMDISK)', 'panic:')
        wait_for_console_pattern(
            self,
            '(I)nstall, (U)pgrade, (A)utoinstall or (S)hell?',
            'panic:')


if __name__ == '__main__':
    QemuSystemTest.main()
