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
from os.path import join
import shutil

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


class Aarch64RMESbsaRefMachine(QemuSystemTest):

    # Stack is inspired from:
    # https://linaro.atlassian.net/wiki/spaces/QEMU/pages/29051027459/
    # https://github.com/pbo-linaro/qemu-linux-stack/tree/rme_sbsa_release
    # ./build.sh && ./archive_artifacts.sh out.tar.xz
    ASSET_RME_STACK_SBSA = Asset(
        ('https://github.com/pbo-linaro/qemu-linux-stack/'
         'releases/download/build/rme_sbsa_release-6a2dfc5.tar.xz'),
         '5adba482aa069912292a8da746c6b21268224d9d81c97fe7c0bed690579ebdcb')

    # This tests the FEAT_RME cpu implementation, by booting a VM supporting it,
    # and launching a nested VM using it.
    def test_aarch64_rme_sbsaref(self):
        self.set_machine('sbsa-ref')
        self.require_accelerator('tcg')
        self.require_netdev('user')

        self.vm.set_console()

        stack_path_tar = self.ASSET_RME_STACK_SBSA.fetch()
        self.archive_extract(stack_path_tar, format="tar")

        rme_stack = self.scratch_file('.')
        pflash0 = join(rme_stack, 'out', 'SBSA_FLASH0.fd')
        pflash1 = join(rme_stack, 'out', 'SBSA_FLASH1.fd')
        rootfs = join(rme_stack, 'out', 'host.ext4')

        efi = join(rme_stack, 'out', 'EFI')
        os.makedirs(efi, exist_ok=True)
        shutil.copyfile(join(rme_stack, 'out', 'Image'), join(efi, 'Image'))
        with open(join(efi, 'startup.nsh'), 'w', encoding='ascii') as startup:
            startup.write('fs0:Image nokaslr root=/dev/vda rw init=/init --'
                          ' /host/out/lkvm run --realm'
                          ' -m 256m'
                          ' --restricted_mem'
                          ' --kernel /host/out/Image'
                          ' --disk /host/out/guest.ext4'
                          ' --params "root=/dev/vda rw init=/init"')

        self.vm.add_args('-cpu', 'max,x-rme=on')
        self.vm.add_args('-smp', '2')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-M', 'sbsa-ref')
        self.vm.add_args('-drive', f'file={pflash0},format=raw,if=pflash')
        self.vm.add_args('-drive', f'file={pflash1},format=raw,if=pflash')
        self.vm.add_args('-drive', f'file=fat:rw:{efi},format=raw')
        self.vm.add_args('-drive', f'format=raw,file={rootfs},if=virtio')
        self.vm.add_args('-virtfs',
                         f'local,path={rme_stack}/,mount_tag=host,'
                         'security_model=mapped,readonly=off')
        self.vm.launch()
        # Wait for host and guest VM boot to complete.
        wait_for_console_pattern(self, 'root@guest',
                                 failure_message='Kernel panic')

if __name__ == '__main__':
    QemuSystemTest.main()
