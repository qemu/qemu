#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import FacebookAspeedTest


class AnacapaMachine(FacebookAspeedTest):

    ASSET_ANACAPA_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/3fa3212827b04be4034d43b5adeef57c27d6ab18/images/anacapa-bmc/openbmc-20260512025228/obmc-phosphor-image-anacapa-20260512025228.static.mtd.xz',
        '2232e241abcfb6d4f6b82cb6c378ce5ce05e364aac6d118785c2b6cc33fe43f3')

    def test_arm_ast2600_anacapa_openbmc(self):
        image_path = self.uncompress(self.ASSET_ANACAPA_FLASH)

        self.do_test_arm_aspeed_openbmc('anacapa-bmc', image=image_path,
                                        uboot='2019.04', cpu_id='0xf00',
                                        soc='AST2600 rev A3')

if __name__ == '__main__':
    FacebookAspeedTest.main()
