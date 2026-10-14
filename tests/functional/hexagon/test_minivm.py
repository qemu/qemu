#!/usr/bin/env python3
#
# Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset

class MiniVMTest(QemuSystemTest):
    '''
    minivm is a Hexagon hypervisor that implements the Hexagon VM
    specification.  These test cases boot minivm and then load test cases
    to the address specified by MiniVMTest.GUEST_ENTRY and
    execute a minvm-guest program to exercise minivm virtualization
    features.
    '''
    timeout = 180
    GUEST_ENTRY = 0xc0000000
    ASSET_TARBALL = Asset(
        'https://github.com/qualcomm/qemu-hexagon-testing/releases/'
        'download/v0.2.21/hexagon_minivm.tar.gz',
        '5980740c5fa505f55cacd1646088831202df4ccbf9e2ddc34b690673574169ba')

    def run_minivm_test(self, test_case):
        self.set_machine('sim')
        self.archive_extract(self.ASSET_TARBALL)
        rootfs_path = self.scratch_file('hexagon-unknown-linux-musl-rootfs')
        kernel_path = f'{rootfs_path}/boot/minivm'

        vm = self.get_vm(name=test_case)
        vm.add_args('-kernel', kernel_path, '-device',
                    f'loader,addr={hex(self.GUEST_ENTRY)},'
                    f'file={rootfs_path}/boot/{test_case}')
        vm.launch()
        vm.wait()
        self.assertEqual(vm.exitcode(), 0)

    def test_mmu(self):
        self.run_minivm_test('test_mmu')

    def test_interrupts(self):
        self.run_minivm_test('test_interrupts')

    def test_processors(self):
        self.run_minivm_test('test_processors')

if __name__ == '__main__':
    QemuSystemTest.main()
