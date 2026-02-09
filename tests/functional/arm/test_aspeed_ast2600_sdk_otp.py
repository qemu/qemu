#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest


class AST2600Machine(AspeedTest):

    ASSET_SDK_V1100_AST2600 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.00/ast2600-default-obmc.tar.gz',
        '64d8926a7d01b649168be96c986603b5690f06391286c438a3a772c8c7039e93')

    def test_arm_ast2600_otp_blockdev_device(self):
        self.vm.set_machine("ast2600-evb")

        image_path = self.archive_extract(self.ASSET_SDK_V1100_AST2600)
        otp_img = self.generate_otpmem_image()

        self.vm.set_console()
        self.vm.add_args(
            "-blockdev", f"driver=file,filename={otp_img},node-name=otp",
            "-global", "aspeed-otp.drive=otp",
        )
        self.do_test_arm_aspeed_sdk_start(
            self.scratch_file("ast2600-default", "image-bmc"))
        self.wait_for_console_pattern("ast2600-default login:")


if __name__ == '__main__':
    AspeedTest.main()
