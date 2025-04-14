#!/usr/bin/env python3
#
# Functional test that boots a various Linux systems and checks the
# console output.
#
# Copyright (c) 2022 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import logging
from subprocess import check_call, DEVNULL

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern, get_qemu_img


class Aarch64VirtMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    timeout = 360

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    ASSET_ALPINE_ISO = Asset(
        ('https://dl-cdn.alpinelinux.org/'
         'alpine/v3.17/releases/aarch64/alpine-standard-3.17.2-aarch64.iso'),
        '5a36304ecf039292082d92b48152a9ec21009d3a62f459de623e19c4bd9dc027')

    # This tests the whole boot chain from EFI to Userspace
    # We only boot a whole OS for the current top level CPU and GIC
    # Other test profiles should use more minimal boots
    def test_alpine_virt_tcg_gic_max(self):
        iso_path = self.ASSET_ALPINE_ISO.fetch()

        self.set_machine('virt')
        self.require_accelerator("tcg")

        self.vm.set_console()
        self.vm.add_args("-accel", "tcg")
        self.vm.add_args("-cpu", "max,pauth-impdef=on")
        self.vm.add_args("-machine",
                         "virt,acpi=on,"
                         "virtualization=on,"
                         "mte=on,"
                         "gic-version=max,iommu=smmuv3")
        self.vm.add_args("-smp", "2", "-m", "1024")
        self.vm.add_args('-bios', self.build_file('pc-bios',
                                                  'edk2-aarch64-code.fd'))
        self.vm.add_args("-drive", f"file={iso_path},media=cdrom,format=raw")
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object', 'rng-random,id=rng0,filename=/dev/urandom')

        self.vm.launch()
        self.wait_for_console_pattern('Welcome to Alpine Linux 3.17')


    ASSET_KERNEL = Asset(
        ('https://fileserver.linaro.org/s/'
         'z6B2ARM7DQT3HWN/download'),
        '12a54d4805cda6ab647cb7c7bbdb16fafb3df400e0d6f16445c1a0436100ef8d')

    def common_aarch64_virt(self, machine):
        """
        Common code to launch basic virt machine with kernel+initrd
        and a scratch disk.
        """
        self.set_machine('virt')
        self.require_accelerator("tcg")

        logger = logging.getLogger('aarch64_virt')

        kernel_path = self.ASSET_KERNEL.fetch()

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        self.vm.add_args('-cpu', 'max,pauth-impdef=on',
                         '-machine', machine,
                         '-accel', 'tcg',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)

        # A RNG offers an easy way to generate a few IRQs
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')

        # Also add a scratch block device
        logger.info('creating scratch qcow2 image')
        image_path = self.scratch_file('scratch.qcow2')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'qcow2', image_path, '8M'],
                   stdout=DEVNULL, stderr=DEVNULL)

        # Add the device
        self.vm.add_args('-blockdev',
                         "driver=qcow2,"
                         "file.driver=file,"
                         f"file.filename={image_path},node-name=scratch")
        self.vm.add_args('-device',
                         'virtio-blk-device,drive=scratch')

        self.vm.launch()

        ps1='#'
        self.wait_for_console_pattern('login:')

        commands = [
            ('root', ps1),
            ('cat /proc/interrupts', ps1),
            ('cat /proc/self/maps', ps1),
            ('uname -a', ps1),
            ('dd if=/dev/hwrng of=/dev/vda bs=512 count=4', ps1),
            ('md5sum /dev/vda', ps1),
            ('halt -n', 'reboot: System halted')
        ]

        for cmd, pattern in commands:
            exec_command_and_wait_for_pattern(self, cmd, pattern)

    def test_aarch64_virt_gicv3(self):
        self.common_aarch64_virt("virt,gic_version=3")

    def test_aarch64_virt_gicv2(self):
        self.common_aarch64_virt("virt,gic-version=2")



if __name__ == '__main__':
    QemuSystemTest.main()
