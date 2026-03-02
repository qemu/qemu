#!/usr/bin/env python3
#
# Functional tests exercising guest KVM file descriptor change on reset.
#
# Copyright © 2026 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
from qemu.machine import machine

from qemu_test import QemuSystemTest, Asset, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern

class KVMGuest(QemuSystemTest):

    # ASSET UKI was generated using
    # https://gitlab.com/kraxel/edk2-tests/-/blob/unittest/tools/make-supermin.sh
    ASSET_UKI = Asset('https://gitlab.com/anisinha/misc-artifacts/'
                      '-/raw/main/uki.x86-64.efi?ref_type=heads',
                      'e0f806bd1fa24111312e1fe849d2ee69808d4343930a5'
                      'dc8c1688da17c65f576')
    # ASSET_OVMF comes from /usr/share/edk2/ovmf/OVMF.stateless.fd of a
    # fedora core 43 distribution which in turn comes from the
    # edk2-ovmf-20251119-3.fc43.noarch rpm of that distribution.
    ASSET_OVMF = Asset('https://gitlab.com/anisinha/misc-artifacts/'
                       '-/raw/main/OVMF.stateless.fd?ref_type=heads',
                       '58a4275aafa8774bd6b1540adceae4ea434b8db75b476'
                       '11839ff47be88cfcf22')

    def common_vm_setup(self, kvm_args=None, cpu_args=None):
        self.set_machine('q35')
        self.require_accelerator("kvm")

        self.vm.set_console()
        if kvm_args:
            self.vm.add_args("-accel", "kvm,%s" %kvm_args)
        else:
            self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-smp", "2")
        if cpu_args:
            self.vm.add_args("-cpu", "host,%s" %cpu_args)
        else:
            self.vm.add_args("-cpu", "host")
        self.vm.add_args("-m", "2G")
        self.vm.add_args("-nographic", "-nodefaults")


        self.uki_path = self.ASSET_UKI.fetch()
        self.ovmf_path = self.ASSET_OVMF.fetch()

        self.vm.add_args('-kernel', self.uki_path)
        self.vm.add_args("-bios", self.ovmf_path)
        # enable KVM VMFD change on reset for a non-coco VM
        self.vm.add_args("-machine", "q35,x-change-vmfd-on-reset=on")

        # enable tracing of basic vmfd change function
        self.vm.add_args("--trace", "kvm_reset_vmfd")

    def launch_vm(self):
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

        self.log.info('VM launched')
        console_pattern = 'bash-5.1#'
        wait_for_console_pattern(self, console_pattern)
        self.log.info('VM ready with a bash prompt')

    def vm_console_reset(self):
        exec_command_and_wait_for_pattern(self, '/usr/sbin/reboot -f',
                                          'reboot: machine restart')
        console_pattern = '# --- Hello world ---'
        wait_for_console_pattern(self, console_pattern)
        self.vm.shutdown()

    def vm_qmp_reset(self):
        self.vm.qmp('system_reset')
        console_pattern = '# --- Hello world ---'
        wait_for_console_pattern(self, console_pattern)
        self.vm.shutdown()

    def check_logs(self):
        self.assertRegex(self.vm.get_log(),
                         r'kvm_reset_vmfd')
        self.assertRegex(self.vm.get_log(),
                         r'virtual machine state has been rebuilt')

    def test_reset_console(self):
        self.common_vm_setup()
        self.launch_vm()
        self.vm_console_reset()
        self.check_logs()

    def test_reset_qmp(self):
        self.common_vm_setup()
        self.launch_vm()
        self.vm_qmp_reset()
        self.check_logs()

    def test_reset_kvmpit(self):
        self.common_vm_setup()
        self.vm.add_args("--trace", "kvmpit_post_vmfd_change")
        self.launch_vm()
        self.vm_console_reset()
        self.assertRegex(self.vm.get_log(),
                         r'kvmpit_post_vmfd_change')

    def test_reset_xen_emulation(self):
        self.common_vm_setup("xen-version=0x4000a,kernel-irqchip=split")
        self.launch_vm()
        self.vm_console_reset()
        self.check_logs()

    def test_reset_hyperv_vmbus(self):
        self.common_vm_setup(None, "hv-syndbg,hv-relaxed,hv_time,hv-synic,"
                             "hv-vpindex,hv-runtime,hv-stimer")
        self.vm.add_args("-device", "vmbus-bridge,irq=15")
        self.vm.add_args("-trace", "vmbus_handle_vmfd_change")
        self.launch_vm()
        self.vm_console_reset()
        self.assertRegex(self.vm.get_log(),
                         r'vmbus_handle_vmfd_change')

if __name__ == '__main__':
    QemuSystemTest.main()
