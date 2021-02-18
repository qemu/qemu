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

from boot_linux_console import LinuxKernelTest


class PluginKernelBase(LinuxKernelTest):
    """
    Boots a Linux kernel with a TCG plugin enabled.
    """

    timeout = 120
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=1 panic=-1 '

    def run_vm(self, kernel_path, kernel_command_line,
               plugin, plugin_log, console_pattern, args):

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

    def _grab_aarch64_kernel(self):
        kernel_url = ('http://security.debian.org/'
                      'debian-security/pool/updates/main/l/linux-signed-arm64/'
                      'linux-image-4.19.0-12-arm64_4.19.152-1_arm64.deb')
        kernel_sha1 = '2036c2792f80ac9c4ccaae742b2e0a28385b6010'
        kernel_deb = self.fetch_asset(kernel_url, asset_hash=kernel_sha1)
        kernel_path = self.extract_from_deb(kernel_deb,
                                            "/boot/vmlinuz-4.19.0-12-arm64")
        return kernel_path

    def test_aarch64_virt_insn(self):
        """
        :avocado: tags=accel:tcg
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=cpu:cortex-a57
        """
        kernel_path = self._grab_aarch64_kernel()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'Kernel panic - not syncing: VFS:'

        plugin_log = tempfile.NamedTemporaryFile(mode="r+t", prefix="plugin",
                                                 suffix=".log")

        self.run_vm(kernel_path, kernel_command_line,
                    "tests/plugin/libinsn.so", plugin_log.name,
                    console_pattern,
                    args=('-cpu', 'cortex-a53'))

        with plugin_log as lf, \
             mmap.mmap(lf.fileno(), 0, access=mmap.ACCESS_READ) as s:

            m = re.search(br"insns: (?P<count>\d+)", s)
            if "count" not in m.groupdict():
                self.fail("Failed to find instruction count")

    def test_aarch64_virt_insn_icount(self):
        """
        :avocado: tags=accel:tcg
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=cpu:cortex-a57
        """
        kernel_path = self._grab_aarch64_kernel()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'Kernel panic - not syncing: VFS:'

        plugin_log = tempfile.NamedTemporaryFile(mode="r+t", prefix="plugin",
                                                 suffix=".log")

        self.run_vm(kernel_path, kernel_command_line,
                    "tests/plugin/libinsn.so", plugin_log.name,
                    console_pattern,
                    args=('-cpu', 'cortex-a53', '-icount', 'shift=1'))

        with plugin_log as lf, \
             mmap.mmap(lf.fileno(), 0, access=mmap.ACCESS_READ) as s:
            m = re.search(br"detected repeat execution @ (?P<addr>0x[0-9A-Fa-f]+)", s)
            if m is not None and "addr" in m.groupdict():
                self.fail("detected repeated instructions")

    def test_aarch64_virt_mem_icount(self):
        """
        :avocado: tags=accel:tcg
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=cpu:cortex-a57
        """
        kernel_path = self._grab_aarch64_kernel()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'Kernel panic - not syncing: VFS:'

        plugin_log = tempfile.NamedTemporaryFile(mode="r+t", prefix="plugin",
                                                 suffix=".log")

        self.run_vm(kernel_path, kernel_command_line,
                    "tests/plugin/libmem.so,arg=both", plugin_log.name,
                    console_pattern,
                    args=('-cpu', 'cortex-a53', '-icount', 'shift=1'))

        with plugin_log as lf, \
             mmap.mmap(lf.fileno(), 0, access=mmap.ACCESS_READ) as s:
            m = re.findall(br"mem accesses: (?P<count>\d+)", s)
            if m is None or len(m) != 2:
                self.fail("no memory access counts found")
            else:
                inline = int(m[0])
                callback = int(m[1])
                if inline != callback:
                    self.fail("mismatched access counts")
