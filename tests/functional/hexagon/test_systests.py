#!/usr/bin/env python3
#
# Copyright(c) Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import time
import unittest

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


_TARBALL_BIN_PATH = (
    "systests_standalone_package",
    "StandaloneSysTests_6.4.0.2_v68",
    "bin",
)

class SysTestsStandaloneTests(QemuSystemTest):
    SYSTEST_TIMEOUT_SEC = 30

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/download/v0.2.11/systests_standalone.tar.gz",
        "b5777aa65245de7710a7a08d717953c1362be7c8b60d9014c9fee8b17610ad1c",
    )

    def setUp(self):
        super().setUp()
        self.archive_extract(self.ASSET_TARBALL)

    def binary(self, name):
        path = self.scratch_file(*_TARBALL_BIN_PATH, name)
        self.assertTrue(os.path.exists(path))
        return path

    def run_exit_zero(self, binary_name, *extra_args, machine="V66G_1024"):
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.launch()
        self.vm.wait(timeout=60.0)
        self.assertEqual(self.vm.exitcode(), 0,
                         f"Test {binary_name} exited with "
                         f"code {self.vm.exitcode()}, expected 0")

    def run_console_pattern(self, binary_name, pattern, *extra_args,
                            machine="V66G_1024"):
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.set_console(semihosting=True)
        self.vm.launch()
        try:
            wait_for_console_pattern(self, pattern)
        finally:
            self.vm.kill()

    def test_fopen(self):
        """fopen reads a file passed via --append and verifies its contents."""
        import tempfile
        # The fopen binary has a short cmdline buffer; use a short path.
        dummy = os.path.join(tempfile.gettempdir(), "qemu_fopen_test.so")
        with open(dummy, "w") as f:
            f.write("valid\n")
        self.run_exit_zero("fopen", "-append", dummy)

    def test_ftrunc(self):
        """ftrunc truncates _testfile_ftrunc from 6 bytes to 1 byte."""
        ftrunc_path = self.scratch_file("_testfile_ftrunc")
        with open(ftrunc_path, "w") as f:
            f.write("valid\n")
        # Sleep 1 s so mtime change is observable
        time.sleep(1)
        self.run_exit_zero("ftrunc", "-append", ftrunc_path)
        self.assertEqual(os.path.getsize(ftrunc_path), 1,
                         "_testfile_ftrunc should be 1 byte after ftrunc")

    def test_access(self):
        """access checks R_OK|W_OK on _testfile_access."""
        testfile = self.scratch_file("_testfile_access")
        with open(testfile, "w") as f:
            f.write("valid\n")
        self.run_exit_zero("access", "-append", testfile)

    def test_semihost(self):
        self.run_console_pattern("semihost", "PASS", "-append", "arg1", "arg2")

if __name__ == "__main__":
    QemuSystemTest.main()
