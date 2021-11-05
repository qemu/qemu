# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import logging

from avocado import skipUnless
from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern


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


class IntegratorMachine(Test):

    timeout = 90

    def boot_integratorcp(self):
        kernel_url = ('https://github.com/zayac/qemu-arm/raw/master/'
                      'arm-test/kernel/zImage.integrator')
        kernel_hash = '0d7adba893c503267c946a3cbdc63b4b54f25468'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = ('https://github.com/zayac/qemu-arm/raw/master/'
                      'arm-test/kernel/arm_root.img')
        initrd_hash = 'b51e4154285bf784e017a37586428332d8c7bd8b'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', 'printk.time=0 console=ttyAMA0')
        self.vm.launch()

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_integratorcp_console(self):
        """
        Boots the Linux kernel and checks that the console is operational
        :avocado: tags=arch:arm
        :avocado: tags=machine:integratorcp
        :avocado: tags=device:pl011
        """
        self.boot_integratorcp()
        wait_for_console_pattern(self, 'Log in as root')

    @skipUnless(NUMPY_AVAILABLE, 'Python NumPy not installed')
    @skipUnless(CV2_AVAILABLE, 'Python OpenCV not installed')
    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_framebuffer_tux_logo(self):
        """
        Boot Linux and verify the Tux logo is displayed on the framebuffer.
        :avocado: tags=arch:arm
        :avocado: tags=machine:integratorcp
        :avocado: tags=device:pl110
        :avocado: tags=device:framebuffer
        """
        screendump_path = os.path.join(self.workdir, "screendump.pbm")
        tuxlogo_url = ('https://github.com/torvalds/linux/raw/v2.6.12/'
                       'drivers/video/logo/logo_linux_vga16.ppm')
        tuxlogo_hash = '3991c2ddbd1ddaecda7601f8aafbcf5b02dc86af'
        tuxlogo_path = self.fetch_asset(tuxlogo_url, asset_hash=tuxlogo_hash)

        self.boot_integratorcp()
        framebuffer_ready = 'Console: switching to colour frame buffer device'
        wait_for_console_pattern(self, framebuffer_ready)
        self.vm.command('human-monitor-command', command_line='stop')
        self.vm.command('human-monitor-command',
                        command_line='screendump %s' % screendump_path)
        logger = logging.getLogger('framebuffer')

        cpu_count = 1
        match_threshold = 0.92
        screendump_bgr = cv2.imread(screendump_path)
        screendump_gray = cv2.cvtColor(screendump_bgr, cv2.COLOR_BGR2GRAY)
        result = cv2.matchTemplate(screendump_gray, cv2.imread(tuxlogo_path, 0),
                                   cv2.TM_CCOEFF_NORMED)
        loc = np.where(result >= match_threshold)
        tux_count = 0
        for tux_count, pt in enumerate(zip(*loc[::-1]), start=1):
            logger.debug('found Tux at position [x, y] = %s', pt)
        self.assertGreaterEqual(tux_count, cpu_count)
