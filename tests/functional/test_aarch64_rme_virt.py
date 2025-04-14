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

import os

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command, wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern

def test_realms_guest(test_rme_instance):

    # Boot the (nested) guest VM
    exec_command(test_rme_instance,
                 'qemu-system-aarch64 -M virt,gic-version=3 '
                 '-cpu host -enable-kvm -m 512M '
                 '-M confidential-guest-support=rme0 '
                 '-object rme-guest,id=rme0 '
                 '-device virtio-net-pci,netdev=net0,romfile= '
                 '-netdev user,id=net0 '
                 '-kernel /mnt/out/bin/Image '
                 '-initrd /mnt/out-br/images/rootfs.cpio '
                 '-serial stdio')
    # Detect Realm activation during (nested) guest boot.
    wait_for_console_pattern(test_rme_instance,
                             'SMC_RMI_REALM_ACTIVATE')
    # Wait for (nested) guest boot to complete.
    wait_for_console_pattern(test_rme_instance,
                             'Welcome to Buildroot')
    exec_command_and_wait_for_pattern(test_rme_instance, 'root', '#')
    # query (nested) guest cca report
    exec_command(test_rme_instance, 'cca-workload-attestation report')
    wait_for_console_pattern(test_rme_instance,
                             '"cca-platform-hash-algo-id": "sha-256"')
    wait_for_console_pattern(test_rme_instance,
                             '"cca-realm-hash-algo-id": "sha-512"')
    wait_for_console_pattern(test_rme_instance,
                             '"cca-realm-public-key-hash-algo-id": "sha-256"')

class Aarch64RMEVirtMachine(QemuSystemTest):

    # Stack is built with OP-TEE build environment from those instructions:
    # https://linaro.atlassian.net/wiki/spaces/QEMU/pages/29051027459/
    # https://github.com/pbo-linaro/qemu-rme-stack
    ASSET_RME_STACK_VIRT = Asset(
        ('https://fileserver.linaro.org/s/iaRsNDJp2CXHMSJ/'
         'download/rme-stack-op-tee-4.2.0-cca-v4-qemu_v8.tar.gz'),
         '1851adc232b094384d8b879b9a2cfff07ef3d6205032b85e9b3a4a9ae6b0b7ad')

    # This tests the FEAT_RME cpu implementation, by booting a VM supporting it,
    # and launching a nested VM using it.
    def test_aarch64_rme_virt(self):
        self.set_machine('virt')
        self.require_accelerator('tcg')
        self.require_netdev('user')

        self.vm.set_console()

        stack_path_tar_gz = self.ASSET_RME_STACK_VIRT.fetch()
        self.archive_extract(stack_path_tar_gz, format="tar")

        rme_stack = self.scratch_file('rme-stack-op-tee-4.2.0-cca-v4-qemu_v8')
        kernel = os.path.join(rme_stack, 'out', 'bin', 'Image')
        bios = os.path.join(rme_stack, 'out', 'bin', 'flash.bin')
        drive = os.path.join(rme_stack, 'out-br', 'images', 'rootfs.ext4')

        self.vm.add_args('-cpu', 'max,x-rme=on,pauth-impdef=on')
        self.vm.add_args('-m', '2G')
        self.vm.add_args('-M', 'virt,acpi=off,'
                         'virtualization=on,'
                         'secure=on,'
                         'gic-version=3')
        self.vm.add_args('-bios', bios)
        self.vm.add_args('-kernel', kernel)
        self.vm.add_args('-drive', f'format=raw,if=none,file={drive},id=hd0')
        self.vm.add_args('-device', 'virtio-blk-pci,drive=hd0')
        self.vm.add_args('-device', 'virtio-9p-device,fsdev=shr0,mount_tag=shr0')
        self.vm.add_args('-fsdev', f'local,security_model=none,path={rme_stack},id=shr0')
        self.vm.add_args('-device', 'virtio-net-pci,netdev=net0')
        self.vm.add_args('-netdev', 'user,id=net0')
        # We need to add nokaslr to avoid triggering this sporadic bug:
        # https://gitlab.com/qemu-project/qemu/-/issues/2823
        self.vm.add_args('-append', 'root=/dev/vda nokaslr')

        self.vm.launch()
        # Wait for host VM boot to complete.
        wait_for_console_pattern(self, 'Welcome to Buildroot',
                                 failure_message='Synchronous Exception at')
        exec_command_and_wait_for_pattern(self, 'root', '#')

        test_realms_guest(self)

if __name__ == '__main__':
    QemuSystemTest.main()
