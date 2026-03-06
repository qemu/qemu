#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2500Machine(AspeedTest):

    ASSET_SDK_V1101_AST2500 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.01/ast2500-default-obmc.tar.gz',
        '3faa1188198da2216837be4b53861c483a58c3ad63784089720bf8421e157da1')

    def test_arm_ast2500_evb_sdk(self):
        self.set_machine('ast2500-evb')

        self.archive_extract(self.ASSET_SDK_V1101_AST2500)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2500-default", "image-bmc"))

        self.wait_for_console_pattern('ast2500-default login:')


if __name__ == '__main__':
    AspeedTest.main()
