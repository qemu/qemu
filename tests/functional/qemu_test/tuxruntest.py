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

import os

from qemu_test import QemuSystemTest
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern
from qemu_test import which, get_qemu_img

class TuxRunBaselineTest(QemuSystemTest):

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0'
    # Tests are ~10-40s, allow for --debug/--enable-gcov overhead
    timeout = 100

    def setUp(self):
        super().setUp()

        # We need zstd for all the tuxrun tests
        if which('zstd') is None:
            self.skipTest("zstd not found in $PATH")

        # Pre-init TuxRun specific settings: Most machines work with
        # reasonable defaults but we sometimes need to tweak the
        # config. To avoid open coding everything we store all these
        # details in the metadata for each test.

        # The tuxboot tag matches the root directory
        self.tuxboot = self.arch

        # Most Linux's use ttyS0 for their serial port
        self.console = "ttyS0"

        # Does the machine shutdown QEMU nicely on "halt"
        self.wait_for_shutdown = True

        self.root = "vda"

        # Occasionally we need extra devices to hook things up
        self.extradev = None

        self.qemu_img = get_qemu_img(self)

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def fetch_tuxrun_assets(self, kernel_asset, rootfs_asset, dtb_asset=None):
        """
        Fetch the TuxBoot assets.
        """
        kernel_image =  kernel_asset.fetch()
        disk_image = self.uncompress(rootfs_asset)
        dtb = dtb_asset.fetch() if dtb_asset is not None else None

        return (kernel_image, disk_image, dtb)

    def prepare_run(self, kernel, disk, drive, dtb=None, console_index=0):
        """
        Setup to run and add the common parameters to the system
        """
        self.vm.set_console(console_index=console_index)

        # all block devices are raw ext4's
        blockdev = "driver=raw,file.driver=file," \
            + f"file.filename={disk},node-name=hd0"

        self.kcmd_line = self.KERNEL_COMMON_COMMAND_LINE
        self.kcmd_line += f" root=/dev/{self.root}"
        self.kcmd_line += f" console={self.console}"

        self.vm.add_args('-kernel', kernel,
                         '-append', self.kcmd_line,
                         '-blockdev', blockdev)

        # Sometimes we need extra devices attached
        if self.extradev:
            self.vm.add_args('-device', self.extradev)

        self.vm.add_args('-device',
                         f"{drive},drive=hd0")

        # Some machines need an explicit DTB
        if dtb:
            self.vm.add_args('-dtb', dtb)

    def run_tuxtest_tests(self, haltmsg):
        """
        Wait for the system to boot up, wait for the login prompt and
        then do a few things on the console. Trigger a shutdown and
        wait to exit cleanly.
        """
        ps1='root@tuxtest:~#'
        self.wait_for_console_pattern(self.kcmd_line)
        self.wait_for_console_pattern('tuxtest login:')
        exec_command_and_wait_for_pattern(self, 'root', ps1)
        exec_command_and_wait_for_pattern(self, 'cat /proc/interrupts', ps1)
        exec_command_and_wait_for_pattern(self, 'cat /proc/self/maps', ps1)
        exec_command_and_wait_for_pattern(self, 'uname -a', ps1)
        exec_command_and_wait_for_pattern(self, 'halt', haltmsg)

        # Wait for VM to shut down gracefully if it can
        if self.wait_for_shutdown:
            self.vm.wait()
        else:
            self.vm.shutdown()

    def common_tuxrun(self,
                      kernel_asset,
                      rootfs_asset,
                      dtb_asset=None,
                      drive="virtio-blk-device",
                      haltmsg="reboot: System halted",
                      console_index=0):
        """
        Common path for LKFT tests. Unless we need to do something
        special with the command line we can process most things using
        the tag metadata.
        """
        (kernel, disk, dtb) = self.fetch_tuxrun_assets(kernel_asset, rootfs_asset,
                                                       dtb_asset)

        self.prepare_run(kernel, disk, drive, dtb, console_index)
        self.vm.launch()
        self.run_tuxtest_tests(haltmsg)
        os.remove(disk)
