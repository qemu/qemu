#!/usr/bin/env python3
#
# TCG Plugins tests
#
# These are a little more involved than the basic tests run by check-tcg.
#
# Copyright (c) 2021 Linaro
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import tempfile
import mmap
import re

from qemu_test import LinuxKernelTest, Asset


class PluginKernelBase(LinuxKernelTest):
    """
    Boots a Linux kernel with a TCG plugin enabled.
    """

    timeout = 120
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=1 panic=-1 '

    def run_vm(self, kernel_path, kernel_command_line,
               plugin, plugin_log, console_pattern, args=None):

        vm = self.get_vm()
        vm.set_console()
        vm.add_args('-kernel', kernel_path,
                    '-append', kernel_command_line,
                    '-plugin', plugin,
                    '-d', 'plugin',
                    '-D', plugin_log,
                    '-net', 'none',
                    '-no-reboot')
        if args:
            vm.add_args(*args)

        try:
            vm.launch()
        except:
            # TODO: probably fails because plugins not enabled but we
            # can't currently probe for the feature.
            self.cancel("TCG Plugins not enabled?")

        self.wait_for_console_pattern(console_pattern, vm)
        # ensure logs are flushed
        vm.shutdown()


class PluginKernelNormal(PluginKernelBase):

    ASSET_KERNEL = Asset(
        ('https://storage.tuxboot.com/20230331/arm64/Image'),
        'ce95a7101a5fecebe0fe630deee6bd97b32ba41bc8754090e9ad8961ea8674c7')

    def test_aarch64_virt_insn(self):
        self.set_machine('virt')
        self.cpu='cortex-a53'
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'Kernel panic - not syncing: VFS:'

        plugin_log = tempfile.NamedTemporaryFile(mode="r+t", prefix="plugin",
                                                 suffix=".log")

        self.run_vm(kernel_path, kernel_command_line,
                    "tests/tcg/plugins/libinsn.so", plugin_log.name,
                    console_pattern)

        with plugin_log as lf, \
             mmap.mmap(lf.fileno(), 0, access=mmap.ACCESS_READ) as s:

            m = re.search(br"insns: (?P<count>\d+)", s)
            if "count" not in m.groupdict():
                self.fail("Failed to find instruction count")
            else:
                count = int(m.group("count"))
                self.log.info(f"Counted: {count} instructions")


    def test_aarch64_virt_insn_icount(self):
        self.set_machine('virt')
        self.cpu='cortex-a53'
        kernel_path = self.ASSET_KERNEL.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'Kernel panic - not syncing: VFS:'

        plugin_log = tempfile.NamedTemporaryFile(mode="r+t", prefix="plugin",
                                                 suffix=".log")

        self.run_vm(kernel_path, kernel_command_line,
                    "tests/tcg/plugins/libinsn.so", plugin_log.name,
                    console_pattern,
                    args=('-icount', 'shift=1'))

        with plugin_log as lf, \
             mmap.mmap(lf.fileno(), 0, access=mmap.ACCESS_READ) as s:

            m = re.search(br"insns: (?P<count>\d+)", s)
            if "count" not in m.groupdict():
                self.fail("Failed to find instruction count")
            else:
                count = int(m.group("count"))
                self.log.info(f"Counted: {count} instructions")

if __name__ == '__main__':
    LinuxKernelTest.main()
