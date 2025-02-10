# ...
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import logging
from subprocess import run


def tesseract_ocr(image_path, tesseract_args=''):
    console_logger = logging.getLogger('console')
    console_logger.debug(image_path)
    proc = run(['tesseract', image_path, 'stdout'],
               capture_output=True, encoding='utf8')
    if proc.returncode:
        return None
    lines = []
    for line in proc.stdout.split('\n'):
        sline = line.strip()
        if len(sline):
            console_logger.debug(sline)
            lines += [sline]
    return lines
