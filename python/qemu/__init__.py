# QEMU library
#
# Copyright (C) 2015-2016 Red Hat Inc.
# Copyright (C) 2012 IBM Corp.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Based on qmp.py.
#

import logging
import os

from . import qmp
from . import machine

LOG = logging.getLogger(__name__)

# Mapping host architecture to any additional architectures it can
# support which often includes its 32 bit cousin.
ADDITIONAL_ARCHES = {
    "x86_64" : "i386",
    "aarch64" : "armhf"
}

def kvm_available(target_arch=None):
    host_arch = os.uname()[4]
    if target_arch and target_arch != host_arch:
        if target_arch != ADDITIONAL_ARCHES.get(host_arch):
            return False
    return os.access("/dev/kvm", os.R_OK | os.W_OK)
