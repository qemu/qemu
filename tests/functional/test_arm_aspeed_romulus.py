#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest

class RomulusMachine(AspeedTest):

    ASSET_ROMULUS_FLASH = Asset(
        ('https://github.com/openbmc/openbmc/releases/download/2.9.0/'
         'obmc-phosphor-image-romulus.static.mtd'),
        '820341076803f1955bc31e647a512c79f9add4f5233d0697678bab4604c7bb25')

    def test_arm_ast2500_romulus_openbmc_v2_9_0(self):
        image_path = self.ASSET_ROMULUS_FLASH.fetch()

        self.do_test_arm_aspeed('romulus-bmc', image_path)


if __name__ == '__main__':
    AspeedTest.main()
