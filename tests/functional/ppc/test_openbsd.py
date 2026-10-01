#!/usr/bin/env python3
#
# Test that OpenBSD boots on mac99 and reaches the installer.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class OpenBSDMac99(QemuSystemTest):

    ASSET_CD = Asset(
        'https://ftp.eu.openbsd.org/pub/OpenBSD/7.9/macppc/cd79.iso',
        '227fef3d5089d56ce8376d8bfa28181ce5b19f0eceef0a6d82d58d8e52832b24')

    def test_openbsd_79_installer(self):
        self.set_machine('mac99')
        self.require_accelerator('tcg')
        cdrom_path = self.ASSET_CD.fetch()

        self.vm.set_console()
        self.vm.add_args('-M', 'via=cuda,graphics=off,usb=off',
                         '-cpu', '7400',
                         '-accel', 'tcg,thread=single',
                         '-smp', '1',
                         '-m', '2g',
                         '-nic', 'none',
                         '-device', 'VGA',
                         '-global', 'cuda.x-sr-delay-ns=0',
                         '-prom-env', 'input-device=ttya',
                         '-prom-env', 'output-device=ttya',
                         '-cdrom', cdrom_path,
                         '-boot', 'd',
                         '-no-reboot')
        self.vm.launch()

        wait_for_console_pattern(self, 'OpenBSD 7.9 (RAMDISK)', 'panic:')
        wait_for_console_pattern(
            self,
            '(I)nstall, (U)pgrade, (A)utoinstall or (S)hell?',
            'panic:')


if __name__ == '__main__':
    QemuSystemTest.main()
