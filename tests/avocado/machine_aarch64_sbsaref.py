# Functional test that boots a Linux kernel and checks the console
#
# SPDX-FileCopyrightText: 2023 Linaro Ltd.
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
    """

    timeout = 180

    def fetch_firmware(self):
        """
        Flash volumes generated using:

        - Fedora GNU Toolchain version 13.1.1 20230511 (Red Hat 13.1.1-2)

        - Trusted Firmware-A
          https://github.com/ARM-software/arm-trusted-firmware/tree/c0d8ee38

        - Tianocore EDK II
          https://github.com/tianocore/edk2/tree/0f9283429dd4
          https://github.com/tianocore/edk2-non-osi/tree/f0bb00937ad6
          https://github.com/tianocore/edk2-platforms/tree/7880b92e2a04
        """

        # Secure BootRom (TF-A code)
        fs0_xz_url = (
            "https://fileserver.linaro.org/s/HrYMCjP7MEccjRP/"
            "download/SBSA_FLASH0.fd.xz"
        )
        fs0_xz_hash = "447eff64a90b84ce47703c6ec41fbfc25befaaea"
        tar_xz_path = self.fetch_asset(fs0_xz_url, asset_hash=fs0_xz_hash)
        archive.extract(tar_xz_path, self.workdir)
        fs0_path = os.path.join(self.workdir, "SBSA_FLASH0.fd")

        # Non-secure rom (UEFI and EFI variables)
        fs1_xz_url = (
            "https://fileserver.linaro.org/s/t8foNnMPz74DZZy/"
            "download/SBSA_FLASH1.fd.xz"
        )
        fs1_xz_hash = "13a9a262953787c7fc5a9155dfaa26e703631e02"
        tar_xz_path = self.fetch_asset(fs1_xz_url, asset_hash=fs1_xz_hash)
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

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is not reliable')
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
        wait_for_console_pattern(self, "BL1: v2.9(release):v2.9")
        wait_for_console_pattern(self, "BL1: Booting BL2")

        # Trusted Boot Firmware
        wait_for_console_pattern(self, "BL2: v2.9(release)")
        wait_for_console_pattern(self, "Booting BL31")

        # EL3 Runtime Software
        wait_for_console_pattern(self, "BL31: v2.9(release)")

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
            "-device",
            "virtio-rng-pci,rng=rng0",
            "-object",
            "rng-random,id=rng0,filename=/dev/urandom",
        )

        self.vm.launch()
        wait_for_console_pattern(self, "Welcome to Alpine Linux 3.17")

    def test_sbsaref_alpine_linux_cortex_a57(self):
        """
        :avocado: tags=cpu:cortex-a57
        """
        self.boot_alpine_linux("cortex-a57")

    def test_sbsaref_alpine_linux_neoverse_n1(self):
        """
        :avocado: tags=cpu:max
        """
        self.boot_alpine_linux("neoverse-n1")

    def test_sbsaref_alpine_linux_max(self):
        """
        :avocado: tags=cpu:max
        """
        self.boot_alpine_linux("max,pauth-impdef=on")
