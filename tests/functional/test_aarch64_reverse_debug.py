#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Reverse debugging test
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgalyuk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import Asset, skipIfMissingImports, skipFlakyTest
from reverse_debugging import ReverseDebugging


@skipIfMissingImports('avocado.utils')
class ReverseDebugging_AArch64(ReverseDebugging):

    REG_PC = 32

    KERNEL_ASSET = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/29/Everything/aarch64/os/images/pxeboot/vmlinuz'),
        '7e1430b81c26bdd0da025eeb8fbd77b5dc961da4364af26e771bd39f379cbbf7')

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/2921")
    def test_aarch64_virt(self):
        self.set_machine('virt')
        self.cpu = 'cortex-a53'
        kernel_path = self.KERNEL_ASSET.fetch()
        self.reverse_debugging(args=('-kernel', kernel_path))


if __name__ == '__main__':
    ReverseDebugging.main()
