#!/usr/bin/env python3
#
# Functional test that boots a VM and run OCR on the framebuffer
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time

from qemu_test import QemuSystemTest, Asset
from qemu_test import skipIfMissingImports, skipIfMissingCommands
from qemu_test.tesseract import tesseract_ocr


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
        # Wait for the FPU test to finish, then the display is available, too:
        while True:
            res = self.vm.cmd('human-monitor-command',
                              command_line='info registers')
            if ("F0 = 400e 8400000000000000" in res and
                "F1 = 400e 83ff000000000000" in res and
                "F2 = 400e 83ff000000000000" in res):
                break
            time.sleep(0.1)

        res = self.vm.cmd('human-monitor-command',
                          command_line=f"screendump {screenshot_path}")
        if 'unknown command' in res:
            self.skipTest('screendump not available')

    @skipIfMissingImports("PIL")
    def test_bootrom_framebuffer_size(self):
        self.set_machine('next-cube')
        screenshot_path = self.scratch_file("dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)

        from PIL import Image
        with Image.open(screenshot_path) as image:
            width, height = image.size
        self.assertEqual(width, 1120)
        self.assertEqual(height, 832)

    @skipIfMissingCommands('tesseract')
    def test_bootrom_framebuffer_ocr_with_tesseract(self):
        self.set_machine('next-cube')
        screenshot_path = self.scratch_file("dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)
        lines = tesseract_ocr(screenshot_path)
        text = '\n'.join(lines)
        self.assertIn('Backplane slot', text)
        self.assertIn('Ethernet address', text)
        self.assertIn('Testing the FPU', text)


if __name__ == '__main__':
    QemuSystemTest.main()
