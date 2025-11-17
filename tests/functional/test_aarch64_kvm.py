#!/usr/bin/env python3
#
# Functional test that runs subsets of kvm-unit-tests on Aarch64.
# These can run on TCG and any accelerator supporting nested
# virtualisation.
#
# Copyright (c) 2025 Linaro
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset
from qemu_test import exec_command_and_wait_for_pattern as ec_and_wait
from qemu_test.linuxkernel import LinuxKernelTest


class Aarch64VirtKVMTests(LinuxKernelTest):

    ASSET_KVM_TEST_KERNEL = Asset(
        'https://share.linaro.org/downloadFile?id=Bs8Eb2Wb7yWtkTA',
        '34de4aaea90db5da42729e7d28b77f392c37a2f4da859f889a5234aaf0970696')

    # make it easier to detect successful return to shell
    PS1 = 'RES=[$?] # '
    OK_CMD = 'RES=[0] # '

    # base of tests
    KUT_BASE = "/usr/share/kvm-unit-tests/"

    def _launch_guest(self, kvm_mode="nvhe"):

        self.set_machine('virt')
        kernel_path = self.ASSET_KVM_TEST_KERNEL.fetch()

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               f"console=ttyAMA0 kvm-arm.mode={kvm_mode}")

        self.vm.add_args("-cpu", "cortex-a72")
        self.vm.add_args("-machine", "virt,gic-version=3,virtualization=on",
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.add_args("-smp", "2", "-m", "320")

        self.vm.launch()

        self.wait_for_console_pattern('buildroot login:')
        ec_and_wait(self, 'root', '#')
        ec_and_wait(self, f"export PS1='{self.PS1}'", self.OK_CMD)

    # this is just a smoketest, we don't run all the tests in the image
    def _smoketest_kvm(self):
        ec_and_wait(self, f"{self.KUT_BASE}/selftest-setup", self.OK_CMD)
        ec_and_wait(self, f"{self.KUT_BASE}/selftest-smp", self.OK_CMD)
        ec_and_wait(self, f"{self.KUT_BASE}/selftest-vectors-kernel", self.OK_CMD)
        ec_and_wait(self, f"{self.KUT_BASE}/selftest-vectors-user", self.OK_CMD)

    def test_aarch64_nvhe_selftest(self):
        self._launch_guest("nvhe")
        self._smoketest_kvm()

    def test_aarch64_vhe_selftest(self):
        self._launch_guest("vhe")
        self._smoketest_kvm()

if __name__ == '__main__':
    LinuxKernelTest.main()
