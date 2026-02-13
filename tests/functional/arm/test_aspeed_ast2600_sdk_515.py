#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2600Machine(AspeedTest):

    ASSET_SDK_V1100_AST2600_515 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.00/ast2600-default-515-obmc.tar.gz',
        'ece1a934095378929780f03e7d092e562f4b33b2841b80ad7c3d12a85744c0f6')

    def test_arm_ast2600_evb_sdk_515(self):
        self.set_machine('ast2600-evb')

        self.archive_extract(self.ASSET_SDK_V1100_AST2600_515)

        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-default-515", "image-bmc"))

        self.wait_for_console_pattern('ast2600-default-515 login:')


if __name__ == '__main__':
    AspeedTest.main()
