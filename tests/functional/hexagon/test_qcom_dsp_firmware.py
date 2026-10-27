#!/usr/bin/env python3
#
# Boot the production DSP firmware on the Qualcomm DSP machines
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset, skipUntrustedTest
from qemu_test import wait_for_console_pattern


class QcomDspFirmwareTest(QemuSystemTest):
    """Base class: the firmware is the ELF that -kernel starts running."""

    MACHINE = None
    ASSET_FIRMWARE = None
    MILESTONE = 'Entering main() ...'

    def boot_firmware(self):
        self.set_machine(self.MACHINE)
        firmware = self.ASSET_FIRMWARE.fetch()

        self.vm.set_console(semihosting=True)
        self.vm.add_args('-kernel', firmware)
        self.vm.launch()
        wait_for_console_pattern(self, 'QURT kernel started',
                                 failure_message='Fatal Error')
        wait_for_console_pattern(self, self.MILESTONE,
                                 failure_message='Fatal Error')
        self.vm.kill()


@skipUntrustedTest()
class Qcs6490CdspFirmwareTest(QcomDspFirmwareTest):
    MACHINE = 'qcs6490-cdsp'

    ASSET_FIRMWARE = Asset(
        'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/'
        '20261019/qcom/qcm6490/cdsp.mbn',
        '43e9c89e15ee82daf83214cae34006a8515fddf9db7f645f5eff4b8e3c4ebb64')

    def test_qcs6490_cdsp_firmware(self):
        self.boot_firmware()


@skipUntrustedTest()
class Sa8775pCdspFirmwareTest(QcomDspFirmwareTest):
    MACHINE = 'sa8775p-cdsp'
    ASSET_FIRMWARE = Asset(
        'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/'
        '20261019/qcom/sa8775p/cdsp0.mbn',
        '1d9d8cd1e913b0d3b49a77b4e962564ab481193e6a5697c8f06406b3fd85a8fc')

    def test_sa8775p_cdsp_firmware(self):
        self.boot_firmware()


@skipUntrustedTest()
class Sc8480xpNsp0FirmwareTest(QcomDspFirmwareTest):
    MACHINE = 'sc8480xp-nsp0'
    ASSET_FIRMWARE = Asset(
        'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/'
        '20261019/qcom/glymur/cdsp.mbn',
        '7b22f33ae273d3dfc5f741a967831e7d4fd79ad141e6a1ecee17509eac207109')

    def test_sc8480xp_nsp0_firmware(self):
        self.boot_firmware()


@skipUntrustedTest()
class Sa8797pNsp0FirmwareTest(QcomDspFirmwareTest):
    MACHINE = 'sa8797p-nsp0'
    ASSET_FIRMWARE = Asset(
        'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/'
        '20261019/qcom/nord/cdsp.mbn',
        '410e7ca3b340e80835e464545a218d9376310a09f24bc87de40bc25f7f5e3240')

    def test_sa8797p_nsp0_firmware(self):
        self.boot_firmware()


@skipUntrustedTest()
class Sm8975NspFirmwareTest(QcomDspFirmwareTest):
    MACHINE = 'sm8975-nsp'
    MILESTONE = 'QURT root task started'
    ASSET_FIRMWARE = Asset(
        'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/'
        '20261019/qcom/hawi/cdsp.mbn',
        '7373952413c141fc636aa933ab0c71e7be1d10f5f7dd36be5374827b253c3db5')

    def test_sm8975_nsp_firmware(self):
        self.boot_firmware()


if __name__ == '__main__':
    QemuSystemTest.main()
