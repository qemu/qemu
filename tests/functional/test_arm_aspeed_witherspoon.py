#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class WitherspoonMachine(AspeedTest):

    ASSET_WITHERSPOON_FLASH = Asset(
        'https://github.com/legoater/qemu-aspeed-boot/raw/master/images/witherspoon-bmc/openbmc-20240618035022/obmc-phosphor-image-witherspoon-20240618035022.ubi.mtd',
        '937d9ed449ea6c6cbed983519088a42d0cafe276bcfe4fce07772ca6673f9213')

    def test_arm_ast2500_witherspoon_openbmc(self):
        image_path = self.ASSET_WITHERSPOON_FLASH.fetch()

        self.do_test_arm_aspeed_openbmc('witherspoon-bmc', image=image_path,
                                        uboot='2016.07', cpu_id='0x0',
                                        soc='AST2500 rev A1')

if __name__ == '__main__':
    AspeedTest.main()
