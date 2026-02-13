#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2500Machine(AspeedTest):

    ASSET_SDK_V1100_AST2500_515 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.00/ast2500-default-515-obmc.tar.gz',
        '5732255d4617d98b76bbbc116d331d6ac89906fa212969eb8213fdc4aea86451')

    def test_arm_ast2500_evb_sdk_515(self):
        self.set_machine('ast2500-evb')

        self.archive_extract(self.ASSET_SDK_V1100_AST2500_515)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2500-default-515", "image-bmc"))

        self.wait_for_console_pattern('ast2500-default-515 login:')


if __name__ == '__main__':
    AspeedTest.main()
