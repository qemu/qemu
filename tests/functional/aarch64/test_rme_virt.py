#!/usr/bin/env python3
#
# Functional test that boots a Realms environment on virt machine and a nested
# guest VM using it.
#
# Copyright (c) 2024 Linaro Ltd.
#
# Author: Pierrick Bouvier <pierrick.bouvier@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from os.path import join

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command, wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern

class Aarch64RMEVirtMachine(QemuSystemTest):

    # Stack is inspired from:
    # https://linaro.atlassian.net/wiki/spaces/QEMU/pages/29051027459/
    # https://github.com/pbo-linaro/qemu-linux-stack/tree/rme_release
    # ./build.sh && ./archive_artifacts.sh out.tar.xz
    ASSET_RME_STACK_VIRT = Asset(
        ('https://github.com/pbo-linaro/qemu-linux-stack/'
         'releases/download/build/rme_release-56bc99e.tar.xz'),
         '0e3dc6b8a4b828dbae09c951a40dcb710eded084b32432b50c69cf4173ffa4be')

    # This tests the FEAT_RME cpu implementation, by booting a VM supporting it,
    # and launching a nested VM using it.
    def test_aarch64_rme_virt(self):
        self.set_machine('virt')
        self.require_accelerator('tcg')
        self.require_netdev('user')

        self.vm.set_console()

        stack_path_tar = self.ASSET_RME_STACK_VIRT.fetch()
        self.archive_extract(stack_path_tar, format="tar")

        rme_stack = self.scratch_file('.')
        kernel = join(rme_stack, 'out', 'Image')
        bios = join(rme_stack, 'out', 'flash.bin')
        rootfs = join(rme_stack, 'out', 'host.ext4')

        self.vm.add_args('-cpu', 'max,x-rme=on')
        self.vm.add_args('-smp', '2')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-M', 'virt,acpi=off,'
                         'virtualization=on,'
                         'secure=on,'
                         'gic-version=3')
        self.vm.add_args('-bios', bios)
        self.vm.add_args('-kernel', kernel)
        self.vm.add_args('-drive', f'format=raw,file={rootfs},if=virtio')
        self.vm.add_args('-virtfs',
                         f'local,path={rme_stack}/,mount_tag=host,'
                         'security_model=mapped,readonly=off')
        # We need to add nokaslr to avoid triggering this sporadic bug:
        # https://gitlab.com/qemu-project/qemu/-/issues/2823
        self.vm.add_args('-append',
                         'nokaslr root=/dev/vda rw init=/init --'
                         ' /host/out/lkvm run --realm'
                         ' -m 256m'
                         ' --restricted_mem'
                         ' --kernel /host/out/Image'
                         ' --disk /host/out/guest.ext4'
                         ' --params "root=/dev/vda rw init=/init"')

        self.vm.launch()
        # Wait for host and guest VM boot to complete.
        wait_for_console_pattern(self, 'root@guest',
                                 failure_message='Kernel panic')

if __name__ == '__main__':
    QemuSystemTest.main()
