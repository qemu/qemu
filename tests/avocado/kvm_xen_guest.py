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

import os

from qemu.machine import machine

from avocado_qemu import LinuxSSHMixIn
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern

class KVMXenGuest(QemuSystemTest, LinuxSSHMixIn):
    """
    :avocado: tags=arch:x86_64
    :avocado: tags=machine:q35
    :avocado: tags=accel:kvm
    :avocado: tags=kvm_xen_guest
    """

    KERNEL_DEFAULT = 'printk.time=0 root=/dev/xvda console=ttyS0'

    kernel_path = None
    kernel_params = None

    # Fetch assets from the kvm-xen-guest subdir of my shared test
    # images directory on fileserver.linaro.org where you can find
    # build instructions for how they where assembled.
    def get_asset(self, name, sha1):
        base_url = ('https://fileserver.linaro.org/s/'
                    'kE4nCFLdQcoBF9t/download?'
                    'path=%2Fkvm-xen-guest&files=' )
        url = base_url + name
        # use explicit name rather than failing to neatly parse the
        # URL into a unique one
        return self.fetch_asset(name=name, locations=(url), asset_hash=sha1)

    def common_vm_setup(self):
        # We also catch lack of KVM_XEN support if we fail to launch
        self.require_accelerator("kvm")

        self.vm.set_console()

        self.vm.add_args("-accel", "kvm,xen-version=0x4000a,kernel-irqchip=split")
        self.vm.add_args("-smp", "2")

        self.kernel_path = self.get_asset("bzImage",
                                          "367962983d0d32109998a70b45dcee4672d0b045")
        self.rootfs = self.get_asset("rootfs.ext4",
                                     "f1478401ea4b3fa2ea196396be44315bab2bb5e4")

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
                self.cancel("KVM Xen support is not present "
                            "(need v5.12+ kernel with CONFIG_KVM_XEN)")
            elif "Property 'kvm-accel.xen-version' not found" in e.output:
                self.cancel("QEMU not built with CONFIG_XEN_EMU support")
            else:
                raise e

        self.log.info('VM launched, waiting for sshd')
        console_pattern = 'Starting dropbear sshd: OK'
        wait_for_console_pattern(self, console_pattern, 'Oops')
        self.log.info('sshd ready')
        self.ssh_connect('root', '', False)

        self.ssh_command('cat /proc/cmdline')
        self.ssh_command('dmesg | grep -e "Grant table initialized"')

    def test_kvm_xen_guest(self):
        """
        :avocado: tags=kvm_xen_guest
        """

        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks')
        self.run_and_check()
        self.ssh_command('grep xen-pirq.*msi /proc/interrupts')

    def test_kvm_xen_guest_nomsi(self):
        """
        :avocado: tags=kvm_xen_guest_nomsi
        """

        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks pci=nomsi')
        self.run_and_check()
        self.ssh_command('grep xen-pirq.* /proc/interrupts')

    def test_kvm_xen_guest_noapic_nomsi(self):
        """
        :avocado: tags=kvm_xen_guest_noapic_nomsi
        """

        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks noapic pci=nomsi')
        self.run_and_check()
        self.ssh_command('grep xen-pirq /proc/interrupts')

    def test_kvm_xen_guest_vapic(self):
        """
        :avocado: tags=kvm_xen_guest_vapic
        """

        self.common_vm_setup()
        self.vm.add_args('-cpu', 'host,+xen-vapic')
        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks')
        self.run_and_check()
        self.ssh_command('grep xen-pirq /proc/interrupts')
        self.ssh_command('grep PCI-MSI /proc/interrupts')

    def test_kvm_xen_guest_novector(self):
        """
        :avocado: tags=kvm_xen_guest_novector
        """

        self.common_vm_setup()
        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks' +
                              ' xen_no_vector_callback')
        self.run_and_check()
        self.ssh_command('grep xen-platform-pci /proc/interrupts')

    def test_kvm_xen_guest_novector_nomsi(self):
        """
        :avocado: tags=kvm_xen_guest_novector_nomsi
        """

        self.common_vm_setup()

        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks pci=nomsi' +
                              ' xen_no_vector_callback')
        self.run_and_check()
        self.ssh_command('grep xen-platform-pci /proc/interrupts')

    def test_kvm_xen_guest_novector_noapic(self):
        """
        :avocado: tags=kvm_xen_guest_novector_noapic
        """

        self.common_vm_setup()
        self.kernel_params = (self.KERNEL_DEFAULT +
                              ' xen_emul_unplug=ide-disks' +
                              ' xen_no_vector_callback noapic')
        self.run_and_check()
        self.ssh_command('grep xen-platform-pci /proc/interrupts')
