#!/usr/bin/env python3
#
# Functional test that boots known good tuxboot images the same way
# that tuxrun (www.tuxrun.org) does. This tool is used by things like
# the LKFT project to run regression tests on kernels.
#
# Copyright (c) 2023 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import Asset, skipIfMissingEnv
from qemu_test.tuxruntest import TuxRunBaselineTest

class TuxRunX86Test(TuxRunBaselineTest):

    ASSET_X86_64_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/6.18.54/x86_64/bzImage',
        '24a3ade610c2cc426ff821050de8a35930b0371357d94cc2c334abac6658552b')
    ASSET_X86_64_VMLINUX = Asset(
        'https://storage.tuxboot.com/kernels/6.18.54/x86_64/vmlinux.xz',
        '77bf44cca48c9c1b9c142e16a2311d58b8ae3462b61bce44bcaf9243b5f26628')
    ASSET_X86_64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20260924/x86_64/rootfs.ext4.zst',
        '8e310455b0244414ccff29c507f7ccfd4032e3587cc405e49ab0322738e3e27c')

    def test_x86_64(self):
        self.require_accelerator("tcg")

        self.set_machine('q35')
        self.cpu="Nehalem"
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_X86_64_KERNEL,
                           rootfs_asset=self.ASSET_X86_64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0",
                           accel="tcg")

    def test_x86_64_kvm(self):
        self.require_accelerator("kvm")

        self.set_machine('q35')
        self.cpu="Nehalem"
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun(kernel_asset=self.ASSET_X86_64_KERNEL,
                           rootfs_asset=self.ASSET_X86_64_ROOTFS,
                           drive="driver=ide-hd,bus=ide.0,unit=0",
                           accel="kvm")

    @skipIfMissingEnv("QEMU_TEST_GDB")
    def test_x86_64_kvm_gdb(self):
        self.require_accelerator("kvm")

        self.set_machine('q35')
        self.cpu="Nehalem"
        self.root='sda'
        self.wait_for_shutdown=False
        self.common_tuxrun_gdb(kernel_asset=self.ASSET_X86_64_KERNEL,
                               vmlinux_asset=self.ASSET_X86_64_VMLINUX,
                               rootfs_asset=self.ASSET_X86_64_ROOTFS,
                               drive="driver=ide-hd,bus=ide.0,unit=0",
                               accel="kvm")


if __name__ == '__main__':
    TuxRunBaselineTest.main()
