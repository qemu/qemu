# ...
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import logging

from . import run_cmd

def tesseract_ocr(image_path, tesseract_args=''):
    console_logger = logging.getLogger('console')
    console_logger.debug(image_path)
    (stdout, stderr, ret) = run_cmd(['tesseract', image_path,
                                     'stdout'])
    if ret:
        return None
    lines = []
    for line in stdout.split('\n'):
        sline = line.strip()
        if len(sline):
            console_logger.debug(sline)
            lines += [sline]
    return lines
