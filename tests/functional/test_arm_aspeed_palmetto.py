#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class PalmettoMachine(AspeedTest):

    ASSET_PALMETTO_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/master/images/palmetto-bmc/openbmc-20250128071432/obmc-phosphor-image-palmetto-20250128071432.static.mtd',
        'bce7c392eec75c707a91cfc8fad7ca9a69d7e4f10df936930d65c1cb9897ac81')

    def test_arm_ast2400_palmetto_openbmc(self):
        image_path = self.ASSET_PALMETTO_FLASH.fetch()

        self.do_test_arm_aspeed_openbmc('palmetto-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0x0',
                                        soc='AST2400 rev A1')

if __name__ == '__main__':
    AspeedTest.main()
