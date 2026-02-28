#!/usr/bin/env python3
#
# Functional test that runs the Arm VBSA conformance tests.
#
# Copyright (c) 2026 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import shutil
from subprocess import check_call, DEVNULL

from qemu_test import QemuSystemTest, Asset
from qemu_test import get_qemu_img, skipIfMissingCommands
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern as ec_and_wait


@skipIfMissingCommands("mformat", "mcopy", "mmd")
class Aarch64VirtMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    timeout = 360

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='FAILED',
                                 vm=vm)

    ASSET_VBSA_EFI = Asset(
        'https://github.com/ARM-software/sysarch-acs/raw/refs/heads/main'
        '/prebuilt_images/VBSA/v25.12_VBSA_0.7.0/Vbsa.efi',
        '80f37d2fb86d152d95dec4d05ff099c9e47ee8a89314268e08056b0e1359e1fa')

    ASSET_BSA_SHELL = Asset(
        'https://github.com/ARM-software/sysarch-acs/raw/refs/heads/main'
        '/prebuilt_images/VBSA/v25.12_VBSA_0.7.0/Shell.efi',
        'e526604f0d329b481c6a1f62f7a0db8ea24ce8178b2c6abda8e247425f38775c')

    def test_aarch64_vbsa_uefi_tests(self):
        """
        Launch the UEFI based VBSA test from an EFI file-system
        """

        self.vm.set_console()

        # virt machine wi
        self.set_machine('virt')
        self.vm.add_args('-M', 'virt,gic-version=max,virtualization=on')
        self.vm.add_args('-cpu', 'max', '-m', '1024')

        # We will use the QEMU firmware blobs to boot
        code_path = self.build_file('pc-bios', 'edk2-aarch64-code.fd')
        vars_source = self.build_file('pc-bios', 'edk2-arm-vars.fd')
        vars_path = self.scratch_file('vars.fd')
        shutil.copy(vars_source, vars_path)

        self.vm.add_args('-drive',
                         f'if=pflash,format=raw,readonly=on,file={code_path}')
        self.vm.add_args('-drive', f'if=pflash,format=raw,file={vars_path}')

        # Build an EFI FAT32 file-system for the UEFI tests
        vbsa_efi = self.ASSET_VBSA_EFI.fetch()
        bsa_shell = self.ASSET_BSA_SHELL.fetch()

        img_path = self.scratch_file('vbsa.img')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'raw', img_path, '64M'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mformat', '-i', img_path, '-v', 'VBSA', '::'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mmd', '-i', img_path, '::/EFI'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mmd', '-i', img_path, '::/EFI/BOOT'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mcopy', '-i', img_path, bsa_shell,
                    '::/EFI/BOOT/BOOTAA64.EFI'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mcopy', '-i', img_path, vbsa_efi, '::/Vbsa.efi'],
                   stdout=DEVNULL, stderr=DEVNULL)

        self.vm.add_args('-drive',
                         f'file={img_path},format=raw,if=none,id=drive0')
        self.vm.add_args('-device', 'virtio-blk-pci,drive=drive0')

        self.vm.launch()

        # wait for EFI prompt
        self.wait_for_console_pattern('Shell>')

        # Start the VBSA tests
        ec_and_wait(self, "FS0:Vbsa.efi", 'VBSA Architecture Compliance Suite')

        # could we parse the summary somehow?

        self.wait_for_console_pattern('VBSA tests complete. Reset the system.')


if __name__ == '__main__':
    QemuSystemTest.main()
