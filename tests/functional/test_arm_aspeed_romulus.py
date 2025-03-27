#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class RomulusMachine(AspeedTest):

    ASSET_ROMULUS_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/master/images/romulus-bmc/openbmc-20250128071340/obmc-phosphor-image-romulus-20250128071340.static.mtd',
        '6d031376440c82ed9d087d25e9fa76aea75b42f80daa252ec402c0bc3cf6cf5b')

    def test_arm_ast2500_romulus_openbmc(self):
        image_path = self.ASSET_ROMULUS_FLASH.fetch()

        self.do_test_arm_aspeed_openbmc('romulus-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0x0',
                                        soc='AST2500 rev A1')

if __name__ == '__main__':
    AspeedTest.main()
