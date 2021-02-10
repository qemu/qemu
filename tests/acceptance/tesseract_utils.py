# ...
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import re
import logging

from avocado.utils import process
from avocado.utils.path import find_command, CmdNotFoundError

def tesseract_available(expected_version):
    try:
        find_command('tesseract')
    except CmdNotFoundError:
        return False
    res = process.run('tesseract --version')
    try:
        version = res.stdout_text.split()[1]
    except IndexError:
        version = res.stderr_text.split()[1]
    return int(version.split('.')[0]) == expected_version

    match = re.match(r'tesseract\s(\d)', res)
    if match is None:
        return False
    # now this is guaranteed to be a digit
    return int(match.groups()[0]) == expected_version


def tesseract_ocr(image_path, tesseract_args='', tesseract_version=3):
    console_logger = logging.getLogger('tesseract')
    console_logger.debug(image_path)
    if tesseract_version == 4:
        tesseract_args += ' --oem 1'
    proc = process.run("tesseract {} {} stdout".format(tesseract_args,
                                                       image_path))
    lines = []
    for line in proc.stdout_text.split('\n'):
        sline = line.strip()
        if len(sline):
            console_logger.debug(sline)
            lines += [sline]
    return lines
