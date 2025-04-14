#!/usr/bin/env python3
#
# Functional tests for the little-endian 64-bit MIPS Malta board
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import logging

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import skipIfMissingImports, skipFlakyTest, skipUntrustedTest

from test_mips_malta import mips_check_wheezy


class MaltaMachineConsole(LinuxKernelTest):

    ASSET_KERNEL_2_63_2 = Asset(
        ('http://snapshot.debian.org/archive/debian/'
         '20130217T032700Z/pool/main/l/linux-2.6/'
         'linux-image-2.6.32-5-5kc-malta_2.6.32-48_mipsel.deb'),
        '35eb476f03be589824b0310358f1c447d85e645b88cbcd2ac02b97ef560f9f8d')

    def test_mips64el_malta(self):
        """
        This test requires the ar tool to extract "data.tar.gz" from
        the Debian package.

        The kernel can be rebuilt using this Debian kernel source [1] and
        following the instructions on [2].

        [1] http://snapshot.debian.org/package/linux-2.6/2.6.32-48/
            #linux-source-2.6.32_2.6.32-48
        [2] https://kernel-team.pages.debian.net/kernel-handbook/
            ch-common-tasks.html#s-common-official
        """
        kernel_path = self.archive_extract(
            self.ASSET_KERNEL_2_63_2,
            member='boot/vmlinux-2.6.32-5-5kc-malta')

        self.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    ASSET_KERNEL_3_19_3 = Asset(
        ('https://github.com/philmd/qemu-testing-blob/'
         'raw/9ad2df38/mips/malta/mips64el/'
         'vmlinux-3.19.3.mtoman.20150408'),
        '8d3beb003bc66051ead98e7172139017fcf9ce2172576541c57e86418dfa5ab8')

    ASSET_CPIO_R1 = Asset(
        ('https://github.com/groeck/linux-build-test/'
         'raw/8584a59e/rootfs/mipsel64/'
         'rootfs.mipsel64r1.cpio.gz'),
        '75ba10cd35fb44e32948eeb26974f061b703c81c4ba2fab1ebcacf1d1bec3b61')

    @skipUntrustedTest()
    def test_mips64el_malta_5KEc_cpio(self):
        kernel_path = self.ASSET_KERNEL_3_19_3.fetch()
        initrd_path = self.uncompress(self.ASSET_CPIO_R1)

        self.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 console=tty '
                               + 'rdinit=/sbin/init noreboot')
        self.vm.add_args('-cpu', '5KEc',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'MIPS 5KE')
        exec_command_and_wait_for_pattern(self, 'uname -a',
                                                '3.19.3.mtoman.20150408')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    ASSET_WHEEZY_KERNEL = Asset(
        ('https://people.debian.org/~aurel32/qemu/mipsel/'
         'vmlinux-3.2.0-4-5kc-malta'),
        '5e8b725244c59745bb8b64f5d8f49f25fecfa549f3395fb6d19a3b9e5065b85b')

    ASSET_WHEEZY_DISK = Asset(
        ('https://people.debian.org/~aurel32/qemu/mipsel/'
         'debian_wheezy_mipsel_standard.qcow2'),
        '454f09ae39f7e6461c84727b927100d2c7813841f2a0a5dce328114887ecf914')

    def test_wheezy(self):
        kernel_path = self.ASSET_WHEEZY_KERNEL.fetch()
        image_path = self.ASSET_WHEEZY_DISK.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 root=/dev/sda1')
        mips_check_wheezy(self,
            kernel_path, image_path, kernel_command_line, cpuinfo='MIPS 20Kc',
            dl_file='/boot/initrd.img-3.2.0-4-5kc-malta',
            hsum='7579f8b56c1187c7c04d0dc3c0c56c7a6314c5ddd3a9bf8803ecc7cf8a3be9f8')


@skipIfMissingImports('numpy', 'cv2')
class MaltaMachineFramebuffer(LinuxKernelTest):

    timeout = 30

    ASSET_KERNEL_4_7_0 = Asset(
        ('https://github.com/philmd/qemu-testing-blob/raw/a5966ca4b5/'
         'mips/malta/mips64el/vmlinux-4.7.0-rc1.I6400.gz'),
        '1f64efc59968a3c328672e6b10213fe574bb2308d9d2ed44e75e40be59e9fbc2')

    ASSET_TUXLOGO = Asset(
        ('https://github.com/torvalds/linux/raw/v2.6.12/'
         'drivers/video/logo/logo_linux_vga16.ppm'),
        'b762f0d91ec018887ad1b334543c2fdf9be9fdfc87672b409211efaa3ea0ef79')

    def do_test_i6400_framebuffer_logo(self, cpu_cores_count):
        """
        Boot Linux kernel and check Tux logo is displayed on the framebuffer.
        """

        import numpy as np
        import cv2

        screendump_path = self.scratch_file('screendump.pbm')

        kernel_path = self.uncompress(self.ASSET_KERNEL_4_7_0)

        tuxlogo_path = self.ASSET_TUXLOGO.fetch()

        self.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'clocksource=GIC console=tty0 console=ttyS0')
        self.vm.add_args('-kernel', kernel_path,
                         '-cpu', 'I6400',
                         '-smp', '%u' % cpu_cores_count,
                         '-vga', 'std',
                         '-append', kernel_command_line)
        self.vm.launch()
        framebuffer_ready = 'Console: switching to colour frame buffer device'
        self.wait_for_console_pattern(framebuffer_ready)
        self.vm.cmd('human-monitor-command', command_line='stop')
        self.vm.cmd('human-monitor-command',
                    command_line='screendump %s' % screendump_path)
        logger = logging.getLogger('framebuffer')

        match_threshold = 0.95
        screendump_bgr = cv2.imread(screendump_path, cv2.IMREAD_COLOR)
        tuxlogo_bgr = cv2.imread(tuxlogo_path, cv2.IMREAD_COLOR)
        result = cv2.matchTemplate(screendump_bgr, tuxlogo_bgr,
                                   cv2.TM_CCOEFF_NORMED)
        loc = np.where(result >= match_threshold)
        tuxlogo_count = 0
        h, w = tuxlogo_bgr.shape[:2]
        debug_png = os.getenv('QEMU_TEST_CV2_SCREENDUMP_PNG_PATH')
        for tuxlogo_count, pt in enumerate(zip(*loc[::-1]), start=1):
            logger.debug('found Tux at position (x, y) = %s', pt)
            cv2.rectangle(screendump_bgr, pt,
                          (pt[0] + w, pt[1] + h), (0, 0, 255), 2)
        if debug_png:
            cv2.imwrite(debug_png, screendump_bgr)
        self.assertGreaterEqual(tuxlogo_count, cpu_cores_count)

    def test_mips_malta_i6400_framebuffer_logo_1core(self):
        self.do_test_i6400_framebuffer_logo(1)

    # XXX file tracking bug
    @skipFlakyTest(bug_url=None)
    def test_mips_malta_i6400_framebuffer_logo_7cores(self):
        self.do_test_i6400_framebuffer_logo(7)

    @skipFlakyTest(bug_url=None)
    def test_mips_malta_i6400_framebuffer_logo_8cores(self):
        self.do_test_i6400_framebuffer_logo(8)


from test_mipsel_malta import MaltaMachineYAMON

if __name__ == '__main__':
    LinuxKernelTest.main()
