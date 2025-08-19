#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test for multiprocess qemu on x86

from multiprocess import Multiprocess
from qemu_test import Asset


class X86Multiprocess(Multiprocess):

    ASSET_KERNEL_X86 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')

    ASSET_INITRD_X86 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux'
         '/releases/31/Everything/x86_64/os/images/pxeboot/initrd.img'),
        '3b6cb5c91a14c42e2f61520f1689264d865e772a1f0069e660a800d31dd61fb9')

    def test_multiprocess(self):
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 rdinit=/bin/bash')
        self.do_test(self.ASSET_KERNEL_X86, self.ASSET_INITRD_X86,
                     kernel_command_line, 'pc')


if __name__ == '__main__':
    Multiprocess.main()
