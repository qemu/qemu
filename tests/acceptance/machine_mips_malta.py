# Functional tests for the MIPS Malta board
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import gzip
import logging

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive
from avocado import skipIf


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
class MaltaMachineFramebuffer(Test):

    timeout = 30

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def do_test_i6400_framebuffer_logo(self, cpu_cores_count):
        """
        Boot Linux kernel and check Tux logo is displayed on the framebuffer.
        """
        screendump_path = os.path.join(self.workdir, 'screendump.pbm')

        kernel_url = ('https://github.com/philmd/qemu-testing-blob/raw/'
                      'a5966ca4b5/mips/malta/mips64el/'
                      'vmlinux-4.7.0-rc1.I6400.gz')
        kernel_hash = '096f50c377ec5072e6a366943324622c312045f6'
        kernel_path_gz = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        kernel_path = self.workdir + "vmlinux"
        archive.gzip_uncompress(kernel_path_gz, kernel_path)

        tuxlogo_url = ('https://github.com/torvalds/linux/raw/v2.6.12/'
                       'drivers/video/logo/logo_linux_vga16.ppm')
        tuxlogo_hash = '3991c2ddbd1ddaecda7601f8aafbcf5b02dc86af'
        tuxlogo_path = self.fetch_asset(tuxlogo_url, asset_hash=tuxlogo_hash)

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
        wait_for_console_pattern(self, framebuffer_ready,
                                 failure_message='Kernel panic - not syncing')
        self.vm.command('human-monitor-command', command_line='stop')
        self.vm.command('human-monitor-command',
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
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=cpu:i6400
        """
        self.do_test_i6400_framebuffer_logo(1)

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_mips_malta_i6400_framebuffer_logo_7cores(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=cpu:i6400
        :avocado: tags=mips:smp
        """
        self.do_test_i6400_framebuffer_logo(7)

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_mips_malta_i6400_framebuffer_logo_8cores(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=cpu:i6400
        :avocado: tags=mips:smp
        """
        self.do_test_i6400_framebuffer_logo(8)
