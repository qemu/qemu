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
from typing import List, Optional


LOG = logging.getLogger(__name__)

# Mapping host architecture to any additional architectures it can
# support which often includes its 32 bit cousin.
ADDITIONAL_ARCHES = {
    "x86_64": "i386",
    "aarch64": "armhf",
    "ppc64le": "ppc64",
}


def list_accel(qemu_bin: str) -> List[str]:
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


def kvm_available(target_arch: Optional[str] = None,
                  qemu_bin: Optional[str] = None) -> bool:
    """
    Check if KVM is available using the following heuristic:
      - Kernel module is present in the host;
      - Target and host arches don't mismatch;
      - KVM is enabled in the QEMU binary.

    @param target_arch (str): target architecture
    @param qemu_bin (str): path to the QEMU binary
    @return True if kvm is available, otherwise False.
    """
    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        return False
    if target_arch:
        host_arch = os.uname()[4]
        if target_arch != host_arch:
            if target_arch != ADDITIONAL_ARCHES.get(host_arch):
                return False
    if qemu_bin and "kvm" not in list_accel(qemu_bin):
        return False
    return True


def tcg_available(qemu_bin: str) -> bool:
    """
    Check if TCG is available.

    @param qemu_bin (str): path to the QEMU binary
    """
    return 'tcg' in list_accel(qemu_bin)
