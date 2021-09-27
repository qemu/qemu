# Functional test that boots a VM and run OCR on the framebuffer
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import time

from avocado_qemu import QemuSystemTest
from avocado import skipUnless

from tesseract_utils import tesseract_available, tesseract_ocr

PIL_AVAILABLE = True
try:
    from PIL import Image
except ImportError:
    PIL_AVAILABLE = False


class NextCubeMachine(QemuSystemTest):
    """
    :avocado: tags=arch:m68k
    :avocado: tags=machine:next-cube
    :avocado: tags=device:framebuffer
    """

    timeout = 15

    def check_bootrom_framebuffer(self, screenshot_path):
        rom_url = ('http://www.nextcomputers.org/NeXTfiles/Software/ROM_Files/'
                   '68040_Non-Turbo_Chipset/Rev_2.5_v66.BIN')
        rom_hash = 'b3534796abae238a0111299fc406a9349f7fee24'
        rom_path = self.fetch_asset(rom_url, asset_hash=rom_hash)

        self.vm.add_args('-bios', rom_path)
        self.vm.launch()

        self.log.info('VM launched, waiting for display')
        # TODO: Use avocado.utils.wait.wait_for to catch the
        #       'displaysurface_create 1120x832' trace-event.
        time.sleep(2)

        self.vm.command('human-monitor-command',
                        command_line='screendump %s' % screenshot_path)

    @skipUnless(PIL_AVAILABLE, 'Python PIL not installed')
    def test_bootrom_framebuffer_size(self):
        screenshot_path = os.path.join(self.workdir, "dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)

        width, height = Image.open(screenshot_path).size
        self.assertEqual(width, 1120)
        self.assertEqual(height, 832)

    @skipUnless(tesseract_available(3), 'tesseract v3 OCR tool not available')
    def test_bootrom_framebuffer_ocr_with_tesseract_v3(self):
        screenshot_path = os.path.join(self.workdir, "dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)
        lines = tesseract_ocr(screenshot_path, tesseract_version=3)
        text = '\n'.join(lines)
        self.assertIn('Backplane', text)
        self.assertIn('Ethernet address', text)

    # Tesseract 4 adds a new OCR engine based on LSTM neural networks. The
    # new version is faster and more accurate than version 3. The drawback is
    # that it is still alpha-level software.
    @skipUnless(tesseract_available(4), 'tesseract v4 OCR tool not available')
    def test_bootrom_framebuffer_ocr_with_tesseract_v4(self):
        screenshot_path = os.path.join(self.workdir, "dump.ppm")
        self.check_bootrom_framebuffer(screenshot_path)
        lines = tesseract_ocr(screenshot_path, tesseract_version=4)
        text = '\n'.join(lines)
        self.assertIn('Testing the FPU, SCC', text)
        self.assertIn('System test failed. Error code', text)
        self.assertIn('Boot command', text)
        self.assertIn('Next>', text)
