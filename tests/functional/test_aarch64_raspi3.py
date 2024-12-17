#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel on a Raspberry Pi machine
# and checks the console
#
# Copyright (c) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class Aarch64Raspi3Machine(LinuxKernelTest):

    ASSET_RPI3_UEFI = Asset(
        ('https://github.com/pbatard/RPi3/releases/download/'
         'v1.15/RPi3_UEFI_Firmware_v1.15.zip'),
        '8cff2e979560048b4c84921f41a91893240b9fb71a88f0b5c5d6c8edd994bd5b')

    def test_aarch64_raspi3_atf(self):
        efi_name = 'RPI_EFI.fd'
        efi_fd = self.archive_extract(self.ASSET_RPI3_UEFI, member=efi_name)

        self.set_machine('raspi3b')
        self.vm.set_console(console_index=1)
        self.vm.add_args('-cpu', 'cortex-a53',
                         '-nodefaults',
                         '-device', f'loader,file={efi_fd},force-raw=true')
        self.vm.launch()
        self.wait_for_console_pattern('version UEFI Firmware v1.15')


if __name__ == '__main__':
    LinuxKernelTest.main()
