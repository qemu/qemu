#!/usr/bin/env python3
#
# KVM Xen guest functional tests
#
# Copyright © 2021 Red Hat, Inc.
# Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
# Author:
#  David Woodhouse <dwmw2@infradead.org>
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu.machine import machine

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern

class KVMXenGuest(QemuSystemTest):

    KERNEL_DEFAULT = 'printk.time=0 root=/dev/xvda console=ttyS0 quiet'

    kernel_path = None
    kernel_params = None

    # Fetch assets from the kvm-xen-guest subdir of my shared test
    # images directory on fileserver.linaro.org where you can find
    # build instructions for how they where assembled.
    ASSET_KERNEL = Asset(
        ('https://fileserver.linaro.org/s/kE4nCFLdQcoBF9t/download?'
         'path=%2Fkvm-xen-guest&files=bzImage'),
        'ec0ad7bb8c33c5982baee0a75505fe7dbf29d3ff5d44258204d6307c6fe0132a')

    ASSET_ROOTFS = Asset(
        ('https://fileserver.linaro.org/s/kE4nCFLdQcoBF9t/download?'
         'path=%2Fkvm-xen-guest&files=rootfs.ext4'),
        'b11045d649006c649c184e93339aaa41a8fe20a1a86620af70323252eb29e40b')

    def common_vm_setup(self):
        # We also catch lack of KVM_XEN support if we fail to launch
        self.require_accelerator("kvm")
        self.require_netdev('user')

        self.vm.set_console()

        self.vm.add_args("-accel", "kvm,xen-version=0x4000a,kernel-irqchip=split")
        self.vm.add_args("-smp", "2")

        self.kernel_path = self.ASSET_KERNEL.fetch()
        self.rootfs = self.ASSET_ROOTFS.fetch()

    def run_and_check(self):
        self.vm.add_args('-kernel', self.kernel_path,
                         '-append', self.kernel_params,
                         '-drive',  f"file={self.rootfs},if=none,snapshot=on,format=raw,id=drv0",
                         '-device', 'xen-disk,drive=drv0,vdev=xvda',
                         '-device', 'virtio-net-pci,netdev=unet',
                         '-netdev', 'user,id=unet,hostfwd=:127.0.0.1:0-:22')

        try:
            self.vm.launch()
        except machine.VMLaunchFailure as e:
            if "Xen HVM guest support not present" in e.output:
                self.skipTest("KVM Xen support is not present "
                              "(need v5.12+ kernel with CONFIG_KVM_XEN)")
            elif "Property 'kvm-accel.xen-version' not found" in e.output:
                self.skipTest("QEMU not built with CONFIG_XEN_EMU support")
            else:
                raise e

        self.log.info('VM launched, waiting for sshd')
        console_pattern = 'Starting dropbear sshd: OK'
        wait_for_console_pattern(self, console_pattern, 'Oops')
        self.log.info('sshd ready')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cmdline', 'xen')
        exec_command_and_wait_for_pattern(self, 'dmesg | grep "Grant table"',
                                          'Grant table initialized')
        wait_for_console_pattern(self, '#', 'Oops')

    def test_kvm_xen_guest(self):
        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-pirq.*msi /proc/interrupts',
                                'virtio0-output')

    def test_kvm_xen_guest_nomsi(self):
        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks pci=nomsi')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-pirq.* /proc/interrupts',
                                'virtio0')

    def test_kvm_xen_guest_noapic_nomsi(self):
        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks noapic pci=nomsi')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-pirq /proc/interrupts',
                                'virtio0')

    def test_kvm_xen_guest_vapic(self):
        self.common_vm_setup()
        self.vm.add_args('-cpu', 'host,+xen-vapic')
        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-pirq /proc/interrupts',
                                'acpi')
        wait_for_console_pattern(self, '#')
        exec_command_and_wait_for_pattern(self,
                                'grep PCI-MSI /proc/interrupts',
                                'virtio0-output')

    def test_kvm_xen_guest_novector(self):
        self.common_vm_setup()
        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks' +
                              ' xen_no_vector_callback')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-platform-pci /proc/interrupts',
                                'fasteoi')

    def test_kvm_xen_guest_novector_nomsi(self):
        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks pci=nomsi' +
                              ' xen_no_vector_callback')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-platform-pci /proc/interrupts',
                                'IO-APIC')

    def test_kvm_xen_guest_novector_noapic(self):
        self.common_vm_setup()
        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks' +
                              ' xen_no_vector_callback noapic')
        self.run_and_check()
        exec_command_and_wait_for_pattern(self,
                                'grep xen-platform-pci /proc/interrupts',
                                'XT-PIC')

if __name__ == '__main__':
    QemuSystemTest.main()
