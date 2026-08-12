#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2600Machine(AspeedTest):

    ASSET_SDK_V1103_AST2600_515 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.03/ast2600-default-515-image.tar.gz',
        'c79d0197106f146476e82bb878e5438f6569bd30f3b53fbb520b59bc54f6b7dc')

    def test_arm_ast2600_evb_sdk_515(self):
        self.set_machine('ast2600-evb')

        self.archive_extract(self.ASSET_SDK_V1103_AST2600_515)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-default-515-image", "image-bmc"))

        self.wait_for_console_pattern('login:')


if __name__ == '__main__':
    AspeedTest.main()
