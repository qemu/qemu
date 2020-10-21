# ...
#
# Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import re

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
