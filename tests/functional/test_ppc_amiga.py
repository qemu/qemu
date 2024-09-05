#!/usr/bin/env python3
#
# Test AmigaNG boards
#
# Copyright (c) 2023 BALATON Zoltan
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import subprocess

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern, run_cmd
from zipfile import ZipFile

class AmigaOneMachine(QemuSystemTest):

    timeout = 90

    ASSET_IMAGE = Asset(
        ('https://www.hyperion-entertainment.com/index.php/'
         'downloads?view=download&format=raw&file=25'),
        '8ff39330ba47d4f64de4ee8fd6809e9c010a9ef17fe51e95c3c1d53437cb481f')

    def test_ppc_amigaone(self):
        self.require_accelerator("tcg")
        self.set_machine('amigaone')
        tar_name = 'A1Firmware_Floppy_05-Mar-2005.zip'
        zip_file = self.ASSET_IMAGE.fetch()
        with ZipFile(zip_file, 'r') as zf:
            zf.extractall(path=self.workdir)
        bios_fh = open(self.workdir + "/u-boot-amigaone.bin", "wb")
        subprocess.run(['tail', '-c', '524288',
                        self.workdir + "/floppy_edition/updater.image"],
                        stdout=bios_fh)

        self.vm.set_console()
        self.vm.add_args('-bios', self.workdir + '/u-boot-amigaone.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'FLASH:')

if __name__ == '__main__':
    QemuSystemTest.main()
