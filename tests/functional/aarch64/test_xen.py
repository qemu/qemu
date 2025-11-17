#!/usr/bin/env python3
#
# Functional test that boots a Xen hypervisor with a domU kernel and
# checks the console output is vaguely sane .
#
# Copyright (c) 2020 Linaro
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import Asset, LinuxKernelTest, wait_for_console_pattern


class BootXen(LinuxKernelTest):
    """
    Boots a Xen hypervisor with a Linux DomU kernel.
    """

    timeout = 90
    XEN_COMMON_COMMAND_LINE = 'dom0_mem=128M loglvl=all guest_loglvl=all'

    ASSET_KERNEL = Asset(
        'https://share.linaro.org/downloadFile?id=RRahAWwAwYKTZQd',
        '00366fa51ea957c19462d2e2aefd480bef80ce727120e714ae48e0c88f261edb')

    def launch_xen(self, xen_path):
        """
        Launch Xen with a dom0 guest kernel
        """
        self.require_accelerator("tcg") # virtualization=on
        self.set_machine('virt')
        self.cpu = "cortex-a57"
        self.kernel_path = self.ASSET_KERNEL.fetch()
        self.log.info("launch with xen_path: %s", xen_path)

        self.vm.set_console()

        self.vm.add_args('-machine', 'virtualization=on',
                         '-m', '768',
                         '-kernel', xen_path,
                         '-append', self.XEN_COMMON_COMMAND_LINE,
                         '-device',
                         'guest-loader,addr=0x47000000,kernel=%s,bootargs=console=hvc0'
                         % (self.kernel_path))

        self.vm.launch()

        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, "Panic on CPU 0:")

    ASSET_XEN_4_11 = Asset(
        'https://share.linaro.org/downloadFile?id=ALU4n2NGGYbE4fO',
        'b745c2631342f9fcc0147ddc364edb62c20ecfebd430e5a3546e7d7c6891c0bc')

    def test_arm64_xen_411_and_dom0(self):
        # archive of file from https://deb.debian.org/debian/pool/main/x/xen/
        xen_path = self.archive_extract(self.ASSET_XEN_4_11, format='deb',
                                        member="boot/xen-4.11-arm64")
        self.launch_xen(xen_path)

    ASSET_XEN_4_14 = Asset(
        'https://share.linaro.org/downloadFile?id=os4zSXPl7WW4lqX',
        'e930a3293248edabd367d5b4b3b6448b9c99c057096ea8b47228a7870661d5cb')

    def test_arm64_xen_414_and_dom0(self):
        # archive of file from https://deb.debian.org/debian/pool/main/x/xen/
        xen_path = self.archive_extract(self.ASSET_XEN_4_14, format='deb',
                                        member="boot/xen-4.14-arm64")
        self.launch_xen(xen_path)

    ASSET_XEN_4_15 = Asset(
        'https://share.linaro.org/downloadFile?id=jjjG4uTp2wuO4Ks',
        '2a9a8af8acf0231844657cc28baab95bd918b0ee2d493ee4ee6f8846e1358bc9')

    def test_arm64_xen_415_and_dom0(self):
        xen_path = self.archive_extract(self.ASSET_XEN_4_15, format='deb',
                                        member="boot/xen-4.15-unstable")
        self.launch_xen(xen_path)


if __name__ == '__main__':
    LinuxKernelTest.main()
