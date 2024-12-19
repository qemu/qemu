#!/usr/bin/env python3
#
# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import logging

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import skipIfMissingImports, skipUntrustedTest


class IntegratorMachine(QemuSystemTest):

    timeout = 90

    ASSET_KERNEL = Asset(
        ('https://github.com/zayac/qemu-arm/raw/master/'
         'arm-test/kernel/zImage.integrator'),
        '26e7c7e8f943de785d95bd3c74d66451604a9b6a7a3d25dceb279e7548fd8e78')

    ASSET_INITRD = Asset(
        ('https://github.com/zayac/qemu-arm/raw/master/'
         'arm-test/kernel/arm_root.img'),
        'e187c27fb342ad148c7f33475fbed124933e0b3f4be8c74bc4f3426a4793373a')

    ASSET_TUXLOGO = Asset(
        ('https://github.com/torvalds/linux/raw/v2.6.12/'
         'drivers/video/logo/logo_linux_vga16.ppm'),
        'b762f0d91ec018887ad1b334543c2fdf9be9fdfc87672b409211efaa3ea0ef79')

    def boot_integratorcp(self):
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()

        self.set_machine('integratorcp')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', 'printk.time=0 console=ttyAMA0')
        self.vm.launch()

    @skipUntrustedTest()
    def test_integratorcp_console(self):
        """
        Boots the Linux kernel and checks that the console is operational
        """
        self.boot_integratorcp()
        wait_for_console_pattern(self, 'Log in as root')

    @skipIfMissingImports("numpy", "cv2")
    @skipUntrustedTest()
    def test_framebuffer_tux_logo(self):
        """
        Boot Linux and verify the Tux logo is displayed on the framebuffer.
        """
        import numpy as np
        import cv2

        screendump_path = self.scratch_file("screendump.pbm")
        tuxlogo_path = self.ASSET_TUXLOGO.fetch()

        self.boot_integratorcp()
        framebuffer_ready = 'Console: switching to colour frame buffer device'
        wait_for_console_pattern(self, framebuffer_ready)
        self.vm.cmd('human-monitor-command', command_line='stop')
        self.vm.cmd('human-monitor-command',
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

if __name__ == '__main__':
    QemuSystemTest.main()
