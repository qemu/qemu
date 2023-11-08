# Test AmigaNG boards
#
# Copyright (c) 2023 BALATON Zoltan
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado.utils import archive
from avocado.utils import process
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class AmigaOneMachine(QemuSystemTest):

    timeout = 90

    def test_ppc_amigaone(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:amigaone
        :avocado: tags=device:articia
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        tar_name = 'A1Firmware_Floppy_05-Mar-2005.zip'
        tar_url = ('https://www.hyperion-entertainment.com/index.php/'
                   'downloads?view=download&format=raw&file=25')
        tar_hash = 'c52e59bc73e31d8bcc3cc2106778f7ac84f6c755'
        zip_file = self.fetch_asset(tar_name, locations=tar_url,
                                    asset_hash=tar_hash)
        archive.extract(zip_file, self.workdir)
        cmd = f"tail -c 524288 {self.workdir}/floppy_edition/updater.image >{self.workdir}/u-boot-amigaone.bin"
        process.run(cmd, shell=True)

        self.vm.set_console()
        self.vm.add_args('-bios', self.workdir + '/u-boot-amigaone.bin')
        self.vm.launch()
        wait_for_console_pattern(self, 'FLASH:')
