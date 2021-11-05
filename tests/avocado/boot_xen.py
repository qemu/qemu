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

import os

from avocado_qemu import wait_for_console_pattern
from boot_linux_console import LinuxKernelTest


class BootXenBase(LinuxKernelTest):
    """
    Boots a Xen hypervisor with a Linux DomU kernel.
    """

    timeout = 90
    XEN_COMMON_COMMAND_LINE = 'dom0_mem=128M loglvl=all guest_loglvl=all'

    def fetch_guest_kernel(self):
        # Using my own built kernel - which works
        kernel_url = ('https://fileserver.linaro.org/'
                      's/JSsewXGZ6mqxPr5/download?path=%2F&files='
                      'linux-5.9.9-arm64-ajb')
        kernel_sha1 = '4f92bc4b9f88d5ab792fa7a43a68555d344e1b83'
        kernel_path = self.fetch_asset(kernel_url,
                                       asset_hash=kernel_sha1)

        return kernel_path

    def launch_xen(self, xen_path):
        """
        Launch Xen with a dom0 guest kernel
        """
        self.log.info("launch with xen_path: %s", xen_path)
        kernel_path = self.fetch_guest_kernel()

        self.vm.set_console()

        xen_command_line = self.XEN_COMMON_COMMAND_LINE
        self.vm.add_args('-machine', 'virtualization=on',
                         '-m', '768',
                         '-kernel', xen_path,
                         '-append', xen_command_line,
                         '-device',
                         'guest-loader,addr=0x47000000,kernel=%s,bootargs=console=hvc0'
                         % (kernel_path))

        self.vm.launch()

        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, "Panic on CPU 0:")


class BootXen(BootXenBase):

    def test_arm64_xen_411_and_dom0(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=accel:tcg
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        """

        # archive of file from https://deb.debian.org/debian/pool/main/x/xen/
        xen_url = ('https://fileserver.linaro.org/s/JSsewXGZ6mqxPr5/'
                   'download?path=%2F&files='
                   'xen-hypervisor-4.11-arm64_4.11.4%2B37-g3263f257ca-1_arm64.deb')
        xen_sha1 = '034e634d4416adbad1212d59b62bccdcda63e62a'
        xen_deb = self.fetch_asset(xen_url, asset_hash=xen_sha1)
        xen_path = self.extract_from_deb(xen_deb, "/boot/xen-4.11-arm64")

        self.launch_xen(xen_path)

    def test_arm64_xen_414_and_dom0(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=accel:tcg
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        """

        # archive of file from https://deb.debian.org/debian/pool/main/x/xen/
        xen_url = ('https://fileserver.linaro.org/s/JSsewXGZ6mqxPr5/'
                   'download?path=%2F&files='
                   'xen-hypervisor-4.14-arm64_4.14.0%2B80-gd101b417b7-1_arm64.deb')
        xen_sha1 = 'b9d209dd689ed2b393e625303a225badefec1160'
        xen_deb = self.fetch_asset(xen_url, asset_hash=xen_sha1)
        xen_path = self.extract_from_deb(xen_deb, "/boot/xen-4.14-arm64")

        self.launch_xen(xen_path)

    def test_arm64_xen_415_and_dom0(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=accel:tcg
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        """

        xen_url = ('https://fileserver.linaro.org/'
                   's/JSsewXGZ6mqxPr5/download'
                   '?path=%2F&files=xen-upstream-4.15-unstable.deb')
        xen_sha1 = 'fc191172b85cf355abb95d275a24cc0f6d6579d8'
        xen_deb = self.fetch_asset(xen_url, asset_hash=xen_sha1)
        xen_path = self.extract_from_deb(xen_deb, "/boot/xen-4.15-unstable")

        self.launch_xen(xen_path)
