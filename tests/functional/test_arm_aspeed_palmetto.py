#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest

class PalmettoMachine(AspeedTest):

    ASSET_PALMETTO_FLASH = Asset(
        ('https://github.com/openbmc/openbmc/releases/download/2.9.0/'
         'obmc-phosphor-image-palmetto.static.mtd'),
        '3e13bbbc28e424865dc42f35ad672b10f2e82cdb11846bb28fa625b48beafd0d');

    def test_arm_ast2400_palmetto_openbmc_v2_9_0(self):
        image_path = self.ASSET_PALMETTO_FLASH.fetch()

        self.do_test_arm_aspeed('palmetto-bmc', image_path)


if __name__ == '__main__':
    AspeedTest.main()
