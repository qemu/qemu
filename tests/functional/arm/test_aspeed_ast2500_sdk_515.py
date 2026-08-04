#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2500Machine(AspeedTest):

    ASSET_SDK_V1103_AST2500_515 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.03/ast2500-default-515-obmc.tar.gz',
        'f17d3b0a5157bcf73c21c4981f838ea0b76c6406cc4a6409267d57d61758ebb6')

    def test_arm_ast2500_evb_sdk_515(self):
        self.set_machine('ast2500-evb')

        self.archive_extract(self.ASSET_SDK_V1103_AST2500_515)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2500-default-515", "image-bmc"))

        self.wait_for_console_pattern('login:')


if __name__ == '__main__':
    AspeedTest.main()
