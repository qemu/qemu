#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern, exec_command
from qemu_test import exec_command_and_wait_for_pattern


class AST2x00MachineSDK(QemuSystemTest):

    def do_test_aarch64_aspeed_sdk_start(self, image, bus_id):
        bus_str = str(bus_id)
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args(
            '-device',
            f'tmp105,'
            f'bus=aspeed.i2c.bus.{bus_str},'
            f'address=0x4d,'
            f'id=tmp-test-{bus_str}'
        )
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user', '-snapshot')

        self.vm.launch()

    def verify_vbootrom_firmware_flow(self):
        wait_for_console_pattern(self, 'Found valid caliptra flash image')
        wait_for_console_pattern(self, 'Check flash image checksum')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Read abb header')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Read soc manifest')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Load atf image')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Load optee image')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Load uboot image')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Load ssp image')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Load tsp image')
        wait_for_console_pattern(self, 'pass')
        wait_for_console_pattern(self, 'Jumping to BL31 (Trusted Firmware-A)')

    def enable_ast2700_pcie2(self):
        wait_for_console_pattern(self, 'Hit any key to stop autoboot')
        exec_command_and_wait_for_pattern(self, '\012', '=>')
        exec_command_and_wait_for_pattern(self,
            'cp 100420000 403000000 900000', '=>')
        exec_command_and_wait_for_pattern(self,
            'bootm start 403000000', '=>')
        exec_command_and_wait_for_pattern(self, 'bootm loados', '=>')
        exec_command_and_wait_for_pattern(self, 'bootm ramdisk', '=>')
        exec_command_and_wait_for_pattern(self, 'bootm prep', '=>')
        exec_command_and_wait_for_pattern(self,
            'fdt set /soc@14000000/pcie@140d0000 status "okay"', '=>')
        exec_command(self, 'bootm go')

    def verify_openbmc_boot_start(self, enable_pcie=True):
        wait_for_console_pattern(self, 'U-Boot 2023.10')
        if enable_pcie:
            self.enable_ast2700_pcie2()
        wait_for_console_pattern(self, 'Linux version ')

    def verify_openbmc_boot_and_login(self, name, enable_pcie=True):
        self.verify_openbmc_boot_start(enable_pcie)

        wait_for_console_pattern(self, f'{name} login:')
        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc', f'root@{name}:~#')

    ASSET_SDK_V1100_AST2700A1 = Asset(
            'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.00/ast2700-a1-obmc.tar.gz',
            'd5ceed511cd0dfefbb102fff2d731159e0472948a28066dc0d90bcd54be76525')

    ASSET_SDK_V1100_AST2700A1_DCSCM = Asset(
            'https://github.com/AspeedTech-BMC/openbmc/releases/download/v11.00/ast2700-a1-dcscm-obmc.tar.gz',
            '4f8778be176ece1b57d33c4aee13bb989be114c3e4703150eaeb6f996bd5587f')

    def do_ast2700_i2c_test(self, bus_id):
        bus_str = str(bus_id)
        exec_command_and_wait_for_pattern(self,
            f'echo lm75 0x4d > /sys/class/i2c-dev/i2c-{bus_str}/device/new_device ',
            f'i2c i2c-{bus_str}: new_device: Instantiated device lm75 at 0x4d')
        exec_command_and_wait_for_pattern(self,
            f'cat /sys/bus/i2c/devices/{bus_str}-004d/hwmon/hwmon*/temp1_input', '0')
        self.vm.cmd('qom-set', path=f'/machine/peripheral/tmp-test-{bus_str}',
                    property='temperature', value=18000)
        exec_command_and_wait_for_pattern(self,
            f'cat /sys/bus/i2c/devices/{bus_str}-004d/hwmon/hwmon*/temp1_input', '18000')

    def do_ast2700_pcie_test(self):
        exec_command_and_wait_for_pattern(self,
            'lspci -s 0002:00:00.0',
            '0002:00:00.0 PCI bridge: '
            'ASPEED Technology, Inc. AST1150 PCI-to-PCI Bridge')
        exec_command_and_wait_for_pattern(self,
            'lspci -s 0002:01:00.0',
            '0002:01:00.0 Ethernet controller: '
            'Intel Corporation 82574L Gigabit Network Connection')
        exec_command_and_wait_for_pattern(self,
            'ip addr show dev eth2',
            'inet 10.0.2.15/24')

    def start_ast2700_test(self, name, bus_id):
        num_cpu = 4
        load_images_list = [
            {
                'addr': '0x400000000',
                'file': self.scratch_file(name, 'u-boot.bin')
            },
            {
                'addr': '0x430000000',
                'file': self.scratch_file(name, 'bl31.bin')
            },
            {
                'addr': '0x430080000',
                'file': self.scratch_file(name, 'optee', 'tee-raw.bin')
            }
        ]

        for load_image in load_images_list:
            addr = load_image['addr']
            file = load_image['file']
            self.vm.add_args('-device',
                             f'loader,force-raw=on,addr={addr},file={file}')

        for i in range(num_cpu):
            self.vm.add_args('-device',
                             f'loader,addr=0x430000000,cpu-num={i}')

        self.vm.add_args('-smp', str(num_cpu))
        self.do_test_aarch64_aspeed_sdk_start(
            self.scratch_file(name, 'image-bmc'), bus_id)

    def start_ast2700_test_vbootrom(self, name, bus_id):
        self.vm.add_args('-bios', 'ast27x0_bootrom.bin')
        self.do_test_aarch64_aspeed_sdk_start(
                self.scratch_file(name, 'image-bmc'), bus_id)

    def test_aarch64_ast2700a1_evb_sdk_v11_00(self):
        self.set_machine('ast2700a1-evb')
        self.require_netdev('user')

        self.archive_extract(self.ASSET_SDK_V1100_AST2700A1)
        self.vm.add_args('-device', 'e1000e,netdev=net1,bus=pcie.2')
        self.vm.add_args('-netdev', 'user,id=net1')
        self.start_ast2700_test('ast2700-a1', 1)
        self.verify_openbmc_boot_and_login('ast2700-a1')
        self.do_ast2700_i2c_test(1)
        self.do_ast2700_pcie_test()

    def test_aarch64_ast2700a1_evb_sdk_vbootrom_v11_00(self):
        self.set_machine('ast2700a1-evb')
        self.require_netdev('user')

        self.archive_extract(self.ASSET_SDK_V1100_AST2700A1)
        self.vm.add_args('-device', 'e1000e,netdev=net1,bus=pcie.2')
        self.vm.add_args('-netdev', 'user,id=net1')
        self.start_ast2700_test_vbootrom('ast2700-a1', 1)
        self.verify_vbootrom_firmware_flow()
        self.verify_openbmc_boot_start()

    def test_aarch64_ast2700a1_evb_ioexp_v11_00(self):
        self.set_machine('ast2700a1-evb')
        self.require_netdev('user')

        self.archive_extract(self.ASSET_SDK_V1100_AST2700A1_DCSCM)
        self.vm.set_machine('ast2700a1-evb,fmc-model=w25q512jv')
        self.vm.add_args('-device',
                         'tmp105,bus=ioexp0.0,address=0x4d,id=tmp-test-16')
        self.start_ast2700_test('ast2700-a1-dcscm', 8)
        self.verify_openbmc_boot_and_login('ast2700-a1-dcscm', False)
        self.do_ast2700_i2c_test(8)
        self.do_ast2700_i2c_test(16)

if __name__ == '__main__':
    QemuSystemTest.main()
