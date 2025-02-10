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

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import interrupt_interactive_console_until_pattern


def fetch_firmware(test):
    """
    Flash volumes generated using:

    Toolchain from Debian:
    aarch64-linux-gnu-gcc (Debian 12.2.0-14) 12.2.0

    Used components:

    - Trusted Firmware         v2.12.0
    - Tianocore EDK2           edk2-stable202411
    - Tianocore EDK2-platforms 4b3530d

    """

    # Secure BootRom (TF-A code)
    fs0_path = test.uncompress(Aarch64SbsarefMachine.ASSET_FLASH0)

    # Non-secure rom (UEFI and EFI variables)
    fs1_path = test.uncompress(Aarch64SbsarefMachine.ASSET_FLASH1)

    for path in [fs0_path, fs1_path]:
        with open(path, "ab+") as fd:
            fd.truncate(256 << 20)  # Expand volumes to 256MiB

    test.set_machine('sbsa-ref')
    test.vm.set_console()
    test.vm.add_args(
        "-drive", f"if=pflash,file={fs0_path},format=raw",
        "-drive", f"if=pflash,file={fs1_path},format=raw",
    )


class Aarch64SbsarefMachine(QemuSystemTest):
    """
    As firmware runs at a higher privilege level than the hypervisor we
    can only run these tests under TCG emulation.
    """

    timeout = 180

    ASSET_FLASH0 = Asset(
        ('https://artifacts.codelinaro.org/artifactory/linaro-419-sbsa-ref/'
         '20241122-189881/edk2/SBSA_FLASH0.fd.xz'),
        '76eb89d42eebe324e4395329f47447cda9ac920aabcf99aca85424609c3384a5')

    ASSET_FLASH1 = Asset(
        ('https://artifacts.codelinaro.org/artifactory/linaro-419-sbsa-ref/'
         '20241122-189881/edk2/SBSA_FLASH1.fd.xz'),
        'f850f243bd8dbd49c51e061e0f79f1697546938f454aeb59ab7d93e5f0d412fc')

    def test_sbsaref_edk2_firmware(self):

        fetch_firmware(self)

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
        wait_for_console_pattern(self, "BL1: v2.12.0(release):")
        wait_for_console_pattern(self, "BL1: Booting BL2")

        # Trusted Boot Firmware
        wait_for_console_pattern(self, "BL2: v2.12.0(release)")
        wait_for_console_pattern(self, "Booting BL31")

        # EL3 Runtime Software
        wait_for_console_pattern(self, "BL31: v2.12.0(release)")

        # Non-trusted Firmware
        wait_for_console_pattern(self, "UEFI firmware (version 1.0")
        interrupt_interactive_console_until_pattern(self, "QEMU SBSA-REF Machine")

if __name__ == '__main__':
    QemuSystemTest.main()
