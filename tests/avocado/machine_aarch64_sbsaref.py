# Functional test that boots a Linux kernel and checks the console
#
# SPDX-FileCopyrightText: 2023-2024 Linaro Ltd.
# SPDX-FileContributor: Philippe Mathieu-Daudé <philmd@linaro.org>
# SPDX-FileContributor: Marcin Juszkiewicz <marcin.juszkiewicz@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from avocado import skipUnless
from avocado.utils import archive

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern


class Aarch64SbsarefMachine(QemuSystemTest):
    """
    :avocado: tags=arch:aarch64
    :avocado: tags=machine:sbsa-ref
    :avocado: tags=accel:tcg

    As firmware runs at a higher privilege level than the hypervisor we
    can only run these tests under TCG emulation.
    """

    timeout = 180

    def fetch_firmware(self):
        """
        Flash volumes generated using:

        Toolchain from Debian:
        aarch64-linux-gnu-gcc (Debian 12.2.0-14) 12.2.0

        Used components:

        - Trusted Firmware 2.11.0
        - Tianocore EDK2 stable202405
        - Tianocore EDK2-platforms commit 4bbd0ed

        """

        # Secure BootRom (TF-A code)
        fs0_xz_url = (
            "https://artifacts.codelinaro.org/artifactory/linaro-419-sbsa-ref/"
            "20240528-140808/edk2/SBSA_FLASH0.fd.xz"
        )
        fs0_xz_hash = "fa6004900b67172914c908b78557fec4d36a5f784f4c3dd08f49adb75e1892a9"
        tar_xz_path = self.fetch_asset(fs0_xz_url, asset_hash=fs0_xz_hash,
                                      algorithm='sha256')
        archive.extract(tar_xz_path, self.workdir)
        fs0_path = os.path.join(self.workdir, "SBSA_FLASH0.fd")

        # Non-secure rom (UEFI and EFI variables)
        fs1_xz_url = (
            "https://artifacts.codelinaro.org/artifactory/linaro-419-sbsa-ref/"
            "20240528-140808/edk2/SBSA_FLASH1.fd.xz"
        )
        fs1_xz_hash = "5f3747d4000bc416d9641e33ff4ac60c3cc8cb74ca51b6e932e58531c62eb6f7"
        tar_xz_path = self.fetch_asset(fs1_xz_url, asset_hash=fs1_xz_hash,
                                      algorithm='sha256')
        archive.extract(tar_xz_path, self.workdir)
        fs1_path = os.path.join(self.workdir, "SBSA_FLASH1.fd")

        for path in [fs0_path, fs1_path]:
            with open(path, "ab+") as fd:
                fd.truncate(256 << 20)  # Expand volumes to 256MiB

        self.vm.set_console()
        self.vm.add_args(
            "-drive",
            f"if=pflash,file={fs0_path},format=raw",
            "-drive",
            f"if=pflash,file={fs1_path},format=raw",
            "-smp",
            "1",
            "-machine",
            "sbsa-ref",
        )

    def test_sbsaref_edk2_firmware(self):
        """
        :avocado: tags=cpu:cortex-a57
        """

        self.fetch_firmware()
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

    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def boot_alpine_linux(self, cpu):
        self.fetch_firmware()

        iso_url = (
            "https://dl-cdn.alpinelinux.org/"
            "alpine/v3.17/releases/aarch64/alpine-standard-3.17.2-aarch64.iso"
        )

        iso_hash = "5a36304ecf039292082d92b48152a9ec21009d3a62f459de623e19c4bd9dc027"
        iso_path = self.fetch_asset(iso_url, algorithm="sha256", asset_hash=iso_hash)

        self.vm.set_console()
        self.vm.add_args(
            "-cpu",
            cpu,
            "-drive",
            f"file={iso_path},format=raw",
        )

        self.vm.launch()
        wait_for_console_pattern(self, "Welcome to Alpine Linux 3.17")

    def test_sbsaref_alpine_linux_cortex_a57(self):
        """
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=os:linux
        """
        self.boot_alpine_linux("cortex-a57")

    def test_sbsaref_alpine_linux_neoverse_n1(self):
        """
        :avocado: tags=cpu:neoverse-n1
        :avocado: tags=os:linux
        """
        self.boot_alpine_linux("neoverse-n1")

    def test_sbsaref_alpine_linux_max_pauth_off(self):
        """
        :avocado: tags=cpu:max
        :avocado: tags=os:linux
        """
        self.boot_alpine_linux("max,pauth=off")

    def test_sbsaref_alpine_linux_max_pauth_impdef(self):
        """
        :avocado: tags=cpu:max
        :avocado: tags=os:linux
        """
        self.boot_alpine_linux("max,pauth-impdef=on")

    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_sbsaref_alpine_linux_max(self):
        """
        :avocado: tags=cpu:max
        :avocado: tags=os:linux
        """
        self.boot_alpine_linux("max")


    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def boot_openbsd73(self, cpu):
        self.fetch_firmware()

        img_url = (
            "https://cdn.openbsd.org/pub/OpenBSD/7.3/arm64/miniroot73.img"
        )

        img_hash = "7fc2c75401d6f01fbfa25f4953f72ad7d7c18650056d30755c44b9c129b707e5"
        img_path = self.fetch_asset(img_url, algorithm="sha256", asset_hash=img_hash)

        self.vm.set_console()
        self.vm.add_args(
            "-cpu",
            cpu,
            "-drive",
            f"file={img_path},format=raw",
        )

        self.vm.launch()
        wait_for_console_pattern(self,
                                 "Welcome to the OpenBSD/arm64"
                                 " 7.3 installation program.")

    def test_sbsaref_openbsd73_cortex_a57(self):
        """
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=os:openbsd
        """
        self.boot_openbsd73("cortex-a57")

    def test_sbsaref_openbsd73_neoverse_n1(self):
        """
        :avocado: tags=cpu:neoverse-n1
        :avocado: tags=os:openbsd
        """
        self.boot_openbsd73("neoverse-n1")

    def test_sbsaref_openbsd73_max_pauth_off(self):
        """
        :avocado: tags=cpu:max
        :avocado: tags=os:openbsd
        """
        self.boot_openbsd73("max,pauth=off")

    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_sbsaref_openbsd73_max_pauth_impdef(self):
        """
        :avocado: tags=cpu:max
        :avocado: tags=os:openbsd
        """
        self.boot_openbsd73("max,pauth-impdef=on")

    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_sbsaref_openbsd73_max(self):
        """
        :avocado: tags=cpu:max
        :avocado: tags=os:openbsd
        """
        self.boot_openbsd73("max")
