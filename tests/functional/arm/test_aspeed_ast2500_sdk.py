#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2500Machine(AspeedTest):

    ASSET_SDK_V1103_AST2500 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.03/ast2500-default-obmc.tar.gz',
        '8e20cafddca04d73b799918d6f35b08c83c9f024e223a317b0ad71b97b84842f')

    def test_arm_ast2500_evb_sdk(self):
        self.set_machine('ast2500-evb')

        self.archive_extract(self.ASSET_SDK_V1103_AST2500)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2500-default", "image-bmc"))

        self.wait_for_console_pattern('login:')


if __name__ == '__main__':
    AspeedTest.main()
