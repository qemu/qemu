#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test for multiprocess qemu on aarch64

from multiprocess import Multiprocess
from qemu_test import Asset


class Aarch64Multiprocess(Multiprocess):

    ASSET_KERNEL_AARCH64 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/aarch64/os/images/pxeboot/vmlinuz'),
        '3ae07fcafbfc8e4abeb693035a74fe10698faae15e9ccd48882a9167800c1527')

    ASSET_INITRD_AARCH64 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/aarch64/os/images/pxeboot/initrd.img'),
        '9fd230cab10b1dafea41cf00150e6669d37051fad133bd618d2130284e16d526')

    def test_multiprocess(self):
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'rdinit=/bin/bash console=ttyAMA0')
        self.do_test(self.ASSET_KERNEL_AARCH64, self.ASSET_INITRD_AARCH64,
                     kernel_command_line, 'virt,gic-version=3')


if __name__ == '__main__':
    Multiprocess.main()
