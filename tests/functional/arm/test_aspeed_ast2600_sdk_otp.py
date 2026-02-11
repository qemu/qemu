#!/usr/bin/env python3
#
# Functional test that boots the ASPEED machines
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from aspeed import AspeedTest

from qemu_test import wait_for_console_pattern, exec_command
from qemu_test import exec_command_and_wait_for_pattern

class AST2600Machine(AspeedTest):

    ASSET_SDK_V1100_AST2600 = Asset(
        'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.00/ast2600-default-obmc.tar.gz',
        '64d8926a7d01b649168be96c986603b5690f06391286c438a3a772c8c7039e93')

    def test_arm_ast2600_otp_blockdev_device(self):
        self.vm.set_machine("ast2600-evb")
        self.require_netdev('user')

        image_path = self.archive_extract(self.ASSET_SDK_V1100_AST2600)
        otp_img = self.generate_otpmem_image()

        self.vm.set_console()
        self.vm.add_args(
            "-blockdev", f"driver=file,filename={otp_img},node-name=otp",
            "-global", "aspeed-otp.drive=otp",
        )
        self.vm.add_args('-drive', 'file=' +
                self.scratch_file("ast2600-default", "image-bmc") +
                ',if=mtd,format=raw',
                '-net', 'nic', '-net', 'user', '-snapshot')
        self.vm.launch()

        # Set OTP value via uboot command
        wait_for_console_pattern(self, 'Hit any key to stop autoboot:')
        exec_command_and_wait_for_pattern(self, '\012', 'ast#')
        exec_command_and_wait_for_pattern(self,
            'otp pb strap  o 0x30 1', 'ast#')
        # Validate OTP value in uboot stage
        exec_command_and_wait_for_pattern(self,
            'otp read strap 0x30', '0x30      1')
        exec_command_and_wait_for_pattern(self, 'boot',
            "ast2600-default login:")
        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc',
            'root@ast2600-default:~#')
        # Validate OTP value in BMC stage
        exec_command_and_wait_for_pattern(self,
            'otp read strap 0x30', '0x30      1')
        exec_command_and_wait_for_pattern(self,
            'reboot', 'Hit any key to stop autoboot')
        exec_command_and_wait_for_pattern(self, '\012', 'ast#')
        # Validate OTP value in uboot stage
        exec_command_and_wait_for_pattern(self,
            'otp read strap 0x30', '0x30      1')


if __name__ == '__main__':
    AspeedTest.main()
