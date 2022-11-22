# Functional test that boots a various Linux systems and checks the
# console output.
#
# Copyright (c) 2022 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import time
import os

from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern
from avocado_qemu import exec_command
from avocado_qemu import BUILD_DIR

class Aarch64VirtMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    timeout = 360

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def test_alpine_virt_tcg_gic_max(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=accel:tcg
        """
        iso_url = ('https://dl-cdn.alpinelinux.org/'
                   'alpine/v3.16/releases/aarch64/'
                   'alpine-virt-3.16.3-aarch64.iso')

        # Alpine use sha256 so I recalculated this myself
        iso_sha1 = '0683bc089486d55c91bf6607d5ecb93925769bc0'
        iso_path = self.fetch_asset(iso_url, asset_hash=iso_sha1)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        self.require_accelerator("tcg")

        self.vm.add_args("-accel", "tcg")
        self.vm.add_args("-cpu", "max,pauth-impdef=on")
        self.vm.add_args("-machine",
                         "virt,acpi=on,"
                         "virtualization=on,"
                         "mte=on,"
                         "gic-version=max,iommu=smmuv3")
        self.vm.add_args("-smp", "2", "-m", "1024")
        self.vm.add_args('-bios', os.path.join(BUILD_DIR, 'pc-bios',
                                               'edk2-aarch64-code.fd'))
        self.vm.add_args("-drive", f"file={iso_path},format=raw")
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object', 'rng-random,id=rng0,filename=/dev/urandom')

        self.vm.launch()
        self.wait_for_console_pattern('Welcome to Alpine Linux 3.16')


    def test_aarch64_virt(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=accel:tcg
        :avocado: tags=cpu:max
        """
        kernel_url = ('https://fileserver.linaro.org/s/'
                      'z6B2ARM7DQT3HWN/download')

        kernel_hash = 'ed11daab50c151dde0e1e9c9cb8b2d9bd3215347'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        self.require_accelerator("tcg")
        self.vm.add_args('-cpu', 'max,pauth-impdef=on',
                         '-accel', 'tcg',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        self.wait_for_console_pattern('Welcome to Buildroot')
        time.sleep(0.1)
        exec_command(self, 'root')
        time.sleep(0.1)
        exec_command(self, 'cat /proc/self/maps')
        time.sleep(0.1)
