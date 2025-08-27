#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class CatalinaMachine(AspeedTest):

    ASSET_CATALINA_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/a866feb5ef81245b4827a214584bf6bcc72939f6/images/catalina-bmc/obmc-phosphor-image-catalina-20250619123021.static.mtd.xz',
        '287402e1ba021991e06be1d098f509444a02a3d81a73a932f66528b159e864f9')

    def test_arm_ast2600_catalina_openbmc(self):
        image_path = self.uncompress(self.ASSET_CATALINA_FLASH)

        self.do_test_arm_aspeed_openbmc('catalina-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

if __name__ == '__main__':
    AspeedTest.main()
