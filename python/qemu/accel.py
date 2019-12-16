"""
QEMU accel module:

This module provides utilities for discover and check the availability of
accelerators.
"""
# Copyright (C) 2015-2016 Red Hat Inc.
# Copyright (C) 2012 IBM Corp.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

import logging
import os
import subprocess

LOG = logging.getLogger(__name__)

# Mapping host architecture to any additional architectures it can
# support which often includes its 32 bit cousin.
ADDITIONAL_ARCHES = {
    "x86_64" : "i386",
    "aarch64" : "armhf"
}

def list_accel(qemu_bin):
    """
    List accelerators enabled in the QEMU binary.

    @param qemu_bin (str): path to the QEMU binary.
    @raise Exception: if failed to run `qemu -accel help`
    @return a list of accelerator names.
    """
    if not qemu_bin:
        return []
    try:
        out = subprocess.check_output([qemu_bin, '-accel', 'help'],
                                      universal_newlines=True)
    except:
        LOG.debug("Failed to get the list of accelerators in %s", qemu_bin)
        raise
    # Skip the first line which is the header.
    return [acc.strip() for acc in out.splitlines()[1:]]

def kvm_available(target_arch=None):
    host_arch = os.uname()[4]
    if target_arch and target_arch != host_arch:
        if target_arch != ADDITIONAL_ARCHES.get(host_arch):
            return False
    return os.access("/dev/kvm", os.R_OK | os.W_OK)
