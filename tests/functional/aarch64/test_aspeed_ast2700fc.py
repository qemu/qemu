#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern


class AST2x00MachineSDK(QemuSystemTest):

    def do_test_aarch64_aspeed_sdk_start(self, image):
        self.require_netdev('user')
        self.vm.set_console()
        self.vm.add_args('-device',
                         'tmp105,bus=aspeed.i2c.bus.1,address=0x4d,id=tmp-test')
        self.vm.add_args('-device', 'e1000e,netdev=net1,bus=pcie.2')
        self.vm.add_args('-netdev', 'user,id=net1')
        self.vm.add_args('-drive', 'file=' + image + ',if=mtd,format=raw',
                         '-net', 'nic', '-net', 'user', '-snapshot')

        self.vm.launch()

    def verify_openbmc_boot_and_login(self, name):
        wait_for_console_pattern(self, 'U-Boot 2023.10')
        wait_for_console_pattern(self, '## Loading kernel from FIT Image')
        wait_for_console_pattern(self, 'Starting kernel ...')

        wait_for_console_pattern(self, f'{name} login:')
        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, '0penBmc', f'root@{name}:~#')

    def load_ast2700fc_coprocessor(self, name):
        load_elf_list = {
            'ssp': self.scratch_file(name, 'zephyr-aspeed-ssp.elf'),
            'tsp': self.scratch_file(name, 'zephyr-aspeed-tsp.elf')
        }

        for cpu_num, key in enumerate(load_elf_list, start=4):
            file = load_elf_list[key]
            self.vm.add_args('-device',
                             f'loader,file={file},cpu-num={cpu_num}')

    ASSET_SDK_V908_AST2700 = Asset(
            'https://github.com/AspeedTech-BMC/openbmc/releases/download/v09.08/ast2700-default-obmc.tar.gz',
            'eac3dc409b7ea3cd4b03d4792d3cebd469792ad893cb51e1d15f0fc20bd1e2cd')

    def do_ast2700_i2c_test(self):
        exec_command_and_wait_for_pattern(self,
            'echo lm75 0x4d > /sys/class/i2c-dev/i2c-1/device/new_device ',
            'i2c i2c-1: new_device: Instantiated device lm75 at 0x4d')
        exec_command_and_wait_for_pattern(self,
            'cat /sys/bus/i2c/devices/1-004d/hwmon/hwmon*/temp1_input', '0')
        self.vm.cmd('qom-set', path='/machine/peripheral/tmp-test',
                    property='temperature', value=18000)
        exec_command_and_wait_for_pattern(self,
            'cat /sys/bus/i2c/devices/1-004d/hwmon/hwmon*/temp1_input', '18000')

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

    def do_ast2700fc_ssp_test(self):
        self.vm.shutdown()
        self.vm.set_console(console_index=1)
        self.vm.launch()

        exec_command_and_wait_for_pattern(self, '\012', 'ssp_tsp:~$')
        exec_command_and_wait_for_pattern(self, 'version',
                                          'Zephyr version 3.7.1')
        exec_command_and_wait_for_pattern(self, 'md 72c02000 1',
                                          '[72c02000] 06010103')

    def do_ast2700fc_tsp_test(self):
        self.vm.shutdown()
        self.vm.set_console(console_index=2)
        self.vm.launch()

        exec_command_and_wait_for_pattern(self, '\012', 'tsp:~$')
        exec_command_and_wait_for_pattern(self, 'version',
                                          'Zephyr version 3.7.1')
        exec_command_and_wait_for_pattern(self, 'md 72c02000 1',
                                          '[72c02000] 06010103')

    def start_ast2700fc_test(self, name):
        ca35_core = 4
        uboot_size = os.path.getsize(self.scratch_file(name,
                                                       'u-boot-nodtb.bin'))
        uboot_dtb_load_addr = hex(0x400000000 + uboot_size)

        load_images_list = [
            {
                'addr': '0x400000000',
                'file': self.scratch_file(name,
                                          'u-boot-nodtb.bin')
            },
            {
                'addr': str(uboot_dtb_load_addr),
                'file': self.scratch_file(name, 'u-boot.dtb')
            },
            {
                'addr': '0x430000000',
                'file': self.scratch_file(name, 'bl31.bin')
            },
            {
                'addr': '0x430080000',
                'file': self.scratch_file(name, 'optee',
                                          'tee-raw.bin')
            }
        ]

        for load_image in load_images_list:
            addr = load_image['addr']
            file = load_image['file']
            self.vm.add_args('-device',
                             f'loader,force-raw=on,addr={addr},file={file}')

        for i in range(ca35_core):
            self.vm.add_args('-device',
                             f'loader,addr=0x430000000,cpu-num={i}')

        self.load_ast2700fc_coprocessor(name)
        self.do_test_aarch64_aspeed_sdk_start(
                self.scratch_file(name, 'image-bmc'))

    def start_ast2700fc_test_vbootrom(self, name):
        self.vm.add_args('-bios', 'ast27x0_bootrom.bin')
        self.load_ast2700fc_coprocessor(name)
        self.do_test_aarch64_aspeed_sdk_start(
                self.scratch_file(name, 'image-bmc'))

    def test_aarch64_ast2700fc_sdk_v09_08(self):
        self.set_machine('ast2700fc')
        self.require_netdev('user')

        self.archive_extract(self.ASSET_SDK_V908_AST2700)
        self.start_ast2700fc_test('ast2700-default')
        self.verify_openbmc_boot_and_login('ast2700-default')
        self.do_ast2700_i2c_test()
        self.do_ast2700_pcie_test()
        self.do_ast2700fc_ssp_test()
        self.do_ast2700fc_tsp_test()

    def test_aarch64_ast2700fc_sdk_vbootrom_v09_08(self):
        self.set_machine('ast2700fc')

        self.archive_extract(self.ASSET_SDK_V908_AST2700)
        self.start_ast2700fc_test_vbootrom('ast2700-default')
        self.verify_openbmc_boot_and_login('ast2700-default')
        self.do_ast2700fc_ssp_test()
        self.do_ast2700fc_tsp_test()

if __name__ == '__main__':
    QemuSystemTest.main()
