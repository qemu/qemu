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
from qemu_test.utils import gzip_uncompress
from unittest import skipUnless

NUMPY_AVAILABLE = True
try:
    import numpy as np
except ImportError:
    NUMPY_AVAILABLE = False

CV2_AVAILABLE = True
try:
    import cv2
except ImportError:
    CV2_AVAILABLE = False


@skipUnless(NUMPY_AVAILABLE, 'Python NumPy not installed')
@skipUnless(CV2_AVAILABLE, 'Python OpenCV not installed')
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
        screendump_path = os.path.join(self.workdir, 'screendump.pbm')

        kernel_path_gz = self.ASSET_KERNEL_4_7_0.fetch()
        kernel_path = self.workdir + "vmlinux"
        gzip_uncompress(kernel_path_gz, kernel_path)

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
        debug_png = os.getenv('AVOCADO_CV2_SCREENDUMP_PNG_PATH')
        for tuxlogo_count, pt in enumerate(zip(*loc[::-1]), start=1):
            logger.debug('found Tux at position (x, y) = %s', pt)
            cv2.rectangle(screendump_bgr, pt,
                          (pt[0] + w, pt[1] + h), (0, 0, 255), 2)
        if debug_png:
            cv2.imwrite(debug_png, screendump_bgr)
        self.assertGreaterEqual(tuxlogo_count, cpu_cores_count)

    def test_mips_malta_i6400_framebuffer_logo_1core(self):
        self.do_test_i6400_framebuffer_logo(1)

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')
    def test_mips_malta_i6400_framebuffer_logo_7cores(self):
        self.do_test_i6400_framebuffer_logo(7)

    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')
    def test_mips_malta_i6400_framebuffer_logo_8cores(self):
        self.do_test_i6400_framebuffer_logo(8)


if __name__ == '__main__':
    LinuxKernelTest.main()
