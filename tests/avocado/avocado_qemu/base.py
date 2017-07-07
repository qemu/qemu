# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See LICENSE for more details.
#
# Copyright (C) 2017 Red Hat Inc.
#
# Authors:
#  Amador Pahim <apahim@redhat.com>

"""
Base class to provide Avocado tests a simple VM.
"""

import logging
import os
import sys
import uuid


from avocado import Test
from avocado.utils import process
from avocado.utils import path as utils_path

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', '..',
                'scripts'))
import qemu


def _get_qemu_bin(arch):
    git_root = process.system_output('git rev-parse --show-toplevel',
                                     ignore_status=True,
                                     verbose=False)
    qemu_binary = os.path.join(git_root,
                               "%s-softmmu" % arch,
                               "qemu-system-%s" % arch)
    if not os.path.exists(qemu_binary):
        qemu_binary = utils_path.find_command('qemu-system-%s' % arch)
    return qemu_binary


class VM(qemu.QEMUMachine):
    '''A QEMU VM'''

    def __init__(self, qemu_bin=None, arch=None):
        name = "qemu-%d" % os.getpid()
        if arch is None:
            arch = process.system_output('uname -m', ignore_status=True,
                                         verbose=False)
        self.arch = arch
        if qemu_bin is None:
            qemu_bin = _get_qemu_bin(self.arch)
        logging.getLogger('avocado.test').info("Qemu binary: '%s'" % qemu_bin)
        super(VM, self).__init__(qemu_bin, name=name)
