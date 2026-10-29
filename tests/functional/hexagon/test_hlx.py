#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path

from qemu_test import Asset, QemuSystemTest


class HLXSystemTests(QemuSystemTest):
    timeout = 180

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.3.2/hlx_systests.tar.gz",
        "8b4340749d48fe33c23726907ac7d533f801e38f0842fb5f46346aa7b3eaa5a7",
    )

    def run_hlx_test(self, name):
        self.archive_extract(self.ASSET_TARBALL)
        kernel = self.scratch_file("bin", name)
        self.assertTrue(Path(kernel).is_file(), kernel)
        self.set_machine("V85QA_1")
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", kernel)
        self.vm.launch()
        self.vm.wait(timeout=30)
        self.assertEqual(self.vm.exitcode(), 0, self.vm.get_log())
        self.assertIn("PASS", self.vm.get_log())

    def test_ccr(self):
        self.run_hlx_test("hlx_sys_ccr")

    def test_enable(self):
        self.run_hlx_test("hlx_sys_enable")

    def test_packet_values(self):
        self.run_hlx_test("hlx_sys_persist")

    def test_vsp(self):
        self.run_hlx_test("hlx_sys_vsp")

    def test_vtcm(self):
        self.run_hlx_test("hlx_sys_vtcm")


if __name__ == "__main__":
    QemuSystemTest.main()
