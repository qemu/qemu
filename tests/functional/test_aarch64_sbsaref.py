#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# SPDX-FileCopyrightText: 2023-2024 Linaro Ltd.
# SPDX-FileContributor: Philippe Mathieu-Daudé <philmd@linaro.org>
# SPDX-FileContributor: Marcin Juszkiewicz <marcin.juszkiewicz@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import interrupt_interactive_console_until_pattern
from qemu_test.utils import lzma_uncompress
from unittest import skipUnless


class Aarch64SbsarefMachine(QemuSystemTest):
    """
    As firmware runs at a higher privilege level than the hypervisor we
    can only run these tests under TCG emulation.
    """

    timeout = 180

    ASSET_FLASH0 = Asset(
        ('https://artifacts.codelinaro.org/artifactory/linaro-419-sbsa-ref/'
         '20240619-148232/edk2/SBSA_FLASH0.fd.xz'),
        '0c954842a590988f526984de22e21ae0ab9cb351a0c99a8a58e928f0c7359cf7')

    ASSET_FLASH1 = Asset(
        ('https://artifacts.codelinaro.org/artifactory/linaro-419-sbsa-ref/'
         '20240619-148232/edk2/SBSA_FLASH1.fd.xz'),
        'c6ec39374c4d79bb9e9cdeeb6db44732d90bb4a334cec92002b3f4b9cac4b5ee')

    def fetch_firmware(self):
        """
        Flash volumes generated using:

        Toolchain from Debian:
        aarch64-linux-gnu-gcc (Debian 12.2.0-14) 12.2.0

        Used components:

        - Trusted Firmware         v2.11.0
        - Tianocore EDK2           4d4f569924
        - Tianocore EDK2-platforms 3f08401

        """

        # Secure BootRom (TF-A code)
        fs0_xz_path = self.ASSET_FLASH0.fetch()
        fs0_path = os.path.join(self.workdir, "SBSA_FLASH0.fd")
        lzma_uncompress(fs0_xz_path, fs0_path)

        # Non-secure rom (UEFI and EFI variables)
        fs1_xz_path = self.ASSET_FLASH1.fetch()
        fs1_path = os.path.join(self.workdir, "SBSA_FLASH1.fd")
        lzma_uncompress(fs1_xz_path, fs1_path)

        for path in [fs0_path, fs1_path]:
            with open(path, "ab+") as fd:
                fd.truncate(256 << 20)  # Expand volumes to 256MiB

        self.set_machine('sbsa-ref')
        self.vm.set_console()
        self.vm.add_args(
            "-drive", f"if=pflash,file={fs0_path},format=raw",
            "-drive", f"if=pflash,file={fs1_path},format=raw",
        )

    def test_sbsaref_edk2_firmware(self):

        self.fetch_firmware()

        self.vm.add_args('-cpu', 'cortex-a57')
        self.vm.launch()

        # TF-A boot sequence:
        #
        # https://github.com/ARM-software/arm-trusted-firmware/blob/v2.8.0/\
        #     docs/design/trusted-board-boot.rst#trusted-board-boot-sequence
        # https://trustedfirmware-a.readthedocs.io/en/v2.8/\
        #     design/firmware-design.html#cold-boot

        # AP Trusted ROM
        wait_for_console_pattern(self, "Booting Trusted Firmware")
        wait_for_console_pattern(self, "BL1: v2.11.0(release):")
        wait_for_console_pattern(self, "BL1: Booting BL2")

        # Trusted Boot Firmware
        wait_for_console_pattern(self, "BL2: v2.11.0(release)")
        wait_for_console_pattern(self, "Booting BL31")

        # EL3 Runtime Software
        wait_for_console_pattern(self, "BL31: v2.11.0(release)")

        # Non-trusted Firmware
        wait_for_console_pattern(self, "UEFI firmware (version 1.0")
        interrupt_interactive_console_until_pattern(self, "QEMU SBSA-REF Machine")


    ASSET_ALPINE_ISO = Asset(
        ('https://dl-cdn.alpinelinux.org/'
         'alpine/v3.17/releases/aarch64/alpine-standard-3.17.2-aarch64.iso'),
        '5a36304ecf039292082d92b48152a9ec21009d3a62f459de623e19c4bd9dc027')

    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def boot_alpine_linux(self, cpu=None):
        self.fetch_firmware()

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

    @skipUnless(os.getenv('QEMU_TEST_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_sbsaref_alpine_linux_max(self):
        self.boot_alpine_linux("max")


    ASSET_OPENBSD_ISO = Asset(
        ('https://cdn.openbsd.org/pub/OpenBSD/7.3/arm64/miniroot73.img'),
        '7fc2c75401d6f01fbfa25f4953f72ad7d7c18650056d30755c44b9c129b707e5')

    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def boot_openbsd73(self, cpu=None):
        self.fetch_firmware()

        img_path = self.ASSET_OPENBSD_ISO.fetch()

        self.vm.set_console()
        self.vm.add_args(
            "-drive", f"file={img_path},format=raw,snapshot=on",
        )
        if cpu:
            self.vm.add_args("-cpu", cpu)

        self.vm.launch()
        wait_for_console_pattern(self,
                                 "Welcome to the OpenBSD/arm64"
                                 " 7.3 installation program.")

    def test_sbsaref_openbsd73_cortex_a57(self):
        self.boot_openbsd73("cortex-a57")

    def test_sbsaref_openbsd73_default_cpu(self):
        self.boot_openbsd73()

    def test_sbsaref_openbsd73_max_pauth_off(self):
        self.boot_openbsd73("max,pauth=off")

    @skipUnless(os.getenv('QEMU_TEST_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_sbsaref_openbsd73_max_pauth_impdef(self):
        self.boot_openbsd73("max,pauth-impdef=on")

    @skipUnless(os.getenv('QEMU_TEST_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_sbsaref_openbsd73_max(self):
        self.boot_openbsd73("max")


if __name__ == '__main__':
    QemuSystemTest.main()
