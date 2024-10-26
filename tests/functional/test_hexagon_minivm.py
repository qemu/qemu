#!/usr/bin/env python3
#
# Copyright(c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

import os
from glob import glob
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test.utils import archive_extract


class MiniVMTest(QemuSystemTest):

    timeout = 180
    GUEST_ENTRY = 0xc0000000

    REPO = 'https://artifacts.codelinaro.org/artifactory'
    ASSET_TARBALL = \
        Asset(f'{REPO}/codelinaro-toolchain-for-hexagon/'
               '19.1.5/hexagon_minivm_2024_Dec_15.tar.gz',
        'd7920b5ff14bed5a10b23ada7d4eb927ede08635281f25067e0d5711feee2c2a')

    def test_minivm(self):
        self.set_machine('virt')
        tarball_path = self.ASSET_TARBALL.fetch()
        archive_extract(tarball_path, self.workdir)
        rootfs_path = f'{self.workdir}/hexagon-unknown-linux-musl-rootfs'
        kernel_path = f'{rootfs_path}/boot/minivm'

        assert(os.path.exists(kernel_path))
        for test_bin_path in glob(f'{rootfs_path}/boot/test_*'):
            print(f'# Testing "{os.path.basename(test_bin_path)}"')

            vm = self.get_vm()
            vm.add_args('-kernel', kernel_path,
                  '-device',
                  f'loader,addr={hex(self.GUEST_ENTRY)},file={test_bin_path}')
            vm.launch()
            vm.wait()
            self.assertEqual(vm.exitcode(), 0)

if __name__ == '__main__':
    QemuSystemTest.main()
