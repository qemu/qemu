#!/usr/bin/env python3
#
# Functional test that boots a kernel and checks the console
#
# Copyright (c) 2023-2024 Linaro Ltd.
#
# Authors:
#   Philippe Mathieu-Daudé
#   Marcin Juszkiewicz
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, skipSlowTest
from qemu_test import wait_for_console_pattern
from test_aarch64_sbsaref import fetch_firmware


class Aarch64SbsarefAlpine(QemuSystemTest):

    ASSET_ALPINE_ISO = Asset(
        ('https://dl-cdn.alpinelinux.org/'
         'alpine/v3.17/releases/aarch64/alpine-standard-3.17.2-aarch64.iso'),
        '5a36304ecf039292082d92b48152a9ec21009d3a62f459de623e19c4bd9dc027')

    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def boot_alpine_linux(self, cpu=None):
        fetch_firmware(self)

        iso_path = self.ASSET_ALPINE_ISO.fetch()

        self.vm.set_console()
        self.vm.add_args(
            "-drive", f"file={iso_path},media=cdrom,format=raw",
        )
        if cpu:
            self.vm.add_args("-cpu", cpu)

        self.vm.launch()
        wait_for_console_pattern(self, "Welcome to Alpine Linux 3.17")

    def test_sbsaref_alpine_linux_cortex_a57(self):
        self.boot_alpine_linux("cortex-a57")

    def test_sbsaref_alpine_linux_default_cpu(self):
        self.boot_alpine_linux()

    def test_sbsaref_alpine_linux_max_pauth_off(self):
        self.boot_alpine_linux("max,pauth=off")

    def test_sbsaref_alpine_linux_max_pauth_impdef(self):
        self.boot_alpine_linux("max,pauth-impdef=on")

    @skipSlowTest()  # Test might timeout due to PAuth emulation
    def test_sbsaref_alpine_linux_max(self):
        self.boot_alpine_linux("max")


if __name__ == '__main__':
    QemuSystemTest.main()
