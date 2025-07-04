#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class GB200Machine(AspeedTest):

    ASSET_GB200_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/refs/heads/master/images/gb200nvl-obmc/obmc-phosphor-image-gb200nvl-obmc-20250702182348.static.mtd.xz',
        'b84819317cb3dc762895ad507705978ef000bfc77c50c33a63bdd37921db0dbc')

    def test_arm_aspeed_gb200_openbmc(self):
        image_path = self.uncompress(self.ASSET_GB200_FLASH)

        self.do_test_arm_aspeed_openbmc('gb200nvl-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3',
                                        image_hostname='gb200nvl-obmc')

if __name__ == '__main__':
    AspeedTest.main()
