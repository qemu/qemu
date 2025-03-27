#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class BletchleyMachine(AspeedTest):

    ASSET_BLETCHLEY_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/master/images/bletchley-bmc/openbmc-20250128071329/obmc-phosphor-image-bletchley-20250128071329.static.mtd.xz',
        'db21d04d47d7bb2a276f59d308614b4dfb70b9c7c81facbbca40a3977a2d8844')

    def test_arm_ast2600_bletchley_openbmc(self):
        image_path = self.uncompress(self.ASSET_BLETCHLEY_FLASH)

        self.do_test_arm_aspeed_openbmc('bletchley-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

if __name__ == '__main__':
    AspeedTest.main()
