#!/usr/bin/env python3
#
# Functional test that boots a Realms environment on sbsa-ref machine and a
# nested guest VM using it.
#
# Copyright (c) 2024 Linaro Ltd.
#
# Author: Pierrick Bouvier <pierrick.bouvier@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern
from test_aarch64_rme_virt import test_realms_guest


class Aarch64RMESbsaRefMachine(QemuSystemTest):

    # Stack is built with OP-TEE build environment from those instructions:
    # https://linaro.atlassian.net/wiki/spaces/QEMU/pages/29051027459/
    # https://github.com/pbo-linaro/qemu-rme-stack
    ASSET_RME_STACK_SBSA = Asset(
        ('https://fileserver.linaro.org/s/KJyeBxL82mz2r7F/'
         'download/rme-stack-op-tee-4.2.0-cca-v4-sbsa.tar.gz'),
         'dd9ab28ec869bdf3b5376116cb3689103b43433fd5c4bca0f4a8d8b3c104999e')

    # This tests the FEAT_RME cpu implementation, by booting a VM supporting it,
    # and launching a nested VM using it.
    def test_aarch64_rme_sbsaref(self):
        self.set_machine('sbsa-ref')
        self.require_accelerator('tcg')
        self.require_netdev('user')

        self.vm.set_console()

        stack_path_tar_gz = self.ASSET_RME_STACK_SBSA.fetch()
        self.archive_extract(stack_path_tar_gz, format="tar")

        rme_stack = self.scratch_file('rme-stack-op-tee-4.2.0-cca-v4-sbsa')
        pflash0 = os.path.join(rme_stack, 'images', 'SBSA_FLASH0.fd')
        pflash1 = os.path.join(rme_stack, 'images', 'SBSA_FLASH1.fd')
        virtual = os.path.join(rme_stack, 'images', 'disks', 'virtual')
        drive = os.path.join(rme_stack, 'out-br', 'images', 'rootfs.ext4')

        self.vm.add_args('-cpu', 'max,x-rme=on,pauth-impdef=on')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-M', 'sbsa-ref')
        self.vm.add_args('-drive', f'file={pflash0},format=raw,if=pflash')
        self.vm.add_args('-drive', f'file={pflash1},format=raw,if=pflash')
        self.vm.add_args('-drive', f'file=fat:rw:{virtual},format=raw')
        self.vm.add_args('-drive', f'format=raw,if=none,file={drive},id=hd0')
        self.vm.add_args('-device', 'virtio-blk-pci,drive=hd0')
        self.vm.add_args('-device', 'virtio-9p-pci,fsdev=shr0,mount_tag=shr0')
        self.vm.add_args('-fsdev', f'local,security_model=none,path={rme_stack},id=shr0')
        self.vm.add_args('-device', 'virtio-net-pci,netdev=net0')
        self.vm.add_args('-netdev', 'user,id=net0')

        self.vm.launch()
        # Wait for host VM boot to complete.
        wait_for_console_pattern(self, 'Welcome to Buildroot',
                                 failure_message='Synchronous Exception at')
        exec_command_and_wait_for_pattern(self, 'root', '#')

        test_realms_guest(self)

if __name__ == '__main__':
    QemuSystemTest.main()
