#!/usr/bin/env python3
#
# Functional test that boots a VM and run OCR on the framebuffer
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import time

from qemu_test import QemuSystemTest, Asset
from unittest import skipUnless

from qemu_test.tesseract import tesseract_available, tesseract_ocr

PIL_AVAILABLE = True
try:
    from PIL import Image
except ImportError:
    PIL_AVAILABLE = False


class NextCubeMachine(QemuSystemTest):

    timeout = 15

    ASSET_ROM = Asset(('https://sourceforge.net/p/previous/code/1350/tree/'
                       'trunk/src/Rev_2.5_v66.BIN?format=raw'),
                      '1b753890b67095b73e104c939ddf62eca9e7d0aedde5108e3893b0ed9d8000a4')

    def check_bootrom_framebuffer(self, screenshot_path):
        rom_path = self.ASSET_ROM.fetch()

        self.vm.add_args('-bios', rom_path)
        self.vm.launch()

        self.log.info('VM launched, waiting for display')
        # TODO: Use avocado.utils.wait.wait_for to catch the
        #       'displaysurface_create 1120x832' trace-event.
        time.sleep(2)

        self.vm.cmd('human-monitor-command',
                    command_line='screendump %s' % screenshot_path)

    @skipUnless(PIL_AVAILABLE, 'Python PIL not installed')
    def test_bootrom_framebuffer_size(self):
        self.set_machine('next-cube')
        screenshot_path = os.path.join(self.workdir, "dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)

        width, height = Image.open(screenshot_path).size
        self.assertEqual(width, 1120)
        self.assertEqual(height, 832)

    # Tesseract 4 adds a new OCR engine based on LSTM neural networks. The
    # new version is faster and more accurate than version 3. The drawback is
    # that it is still alpha-level software.
    @skipUnless(tesseract_available(4), 'tesseract OCR tool not available')
    def test_bootrom_framebuffer_ocr_with_tesseract(self):
        self.set_machine('next-cube')
        screenshot_path = os.path.join(self.workdir, "dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)
        lines = tesseract_ocr(screenshot_path)
        text = '\n'.join(lines)
        self.assertIn('Testing the FPU', text)
        self.assertIn('System test failed. Error code', text)
        self.assertIn('Boot command', text)
        self.assertIn('Next>', text)

if __name__ == '__main__':
    QemuSystemTest.main()
