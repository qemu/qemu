#!/usr/bin/env python3
#
# Test that OpenBSD boots on a ppc powernv machine and reaches the installer.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern


class OpenBSDPowerNV(QemuSystemTest):

    ASSET_MINIROOT = Asset(
        'https://kirill.korins.ky/pub/qemu-powerpc64-openbsd/miniroot79.img',
        '7829e42b75d81cafd732038b9d63228b79c1f5828d8375872a4bb655e1d6b13c')

    ASSET_BOOTKERNEL = Asset(
        'https://kirill.korins.ky/pub/qemu-powerpc64-openbsd/pnor.BOOTKERNEL',
        '397ce43ce61910e1a2c4f13d301f957e61513a9ec5371bc3e87d3095411fae7b')

    def test_powernv9_openbsd_installer(self):
        self.set_machine('powernv9')
        self.require_accelerator('tcg')

        miniroot_path = self.ASSET_MINIROOT.fetch()
        bootkernel_path = self.ASSET_BOOTKERNEL.fetch()

        self.vm.set_console()
        self.vm.add_args('-cpu', 'power9',
                         '-accel', 'tcg,thread=single',
                         '-smp', '1,cores=1,threads=1',
                         '-m', '2g',
                         '-kernel', bootkernel_path,
                         '-device',
                         'ich9-ahci,id=sata0,bus=pcie.0,addr=0x0',
                         '-drive',
                         f'file={miniroot_path},format=raw,if=none,'
                         'id=bootdisk,snapshot=on',
                         '-device',
                         'ide-hd,bus=sata0.0,unit=0,drive=bootdisk,'
                         'bootindex=1')
        self.vm.launch()

        wait_for_console_pattern(self, 'OpenBSD 7.9 (RAMDISK)', 'panic:')
        wait_for_console_pattern(
            self,
            '(I)nstall, (U)pgrade, (A)utoinstall or (S)hell?',
            'panic:')


if __name__ == '__main__':
    QemuSystemTest.main()
