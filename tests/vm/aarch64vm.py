#!/usr/bin/env python3
#
# VM testing aarch64 library
#
# Copyright 2020 Linaro
#
# Authors:
#  Robert Foley <robert.foley@linaro.org>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#
import os
import sys
import subprocess
import basevm
from qemu.utils import kvm_available

# This is the config needed for current version of QEMU.
# This works for both kvm and tcg.
CURRENT_CONFIG = {
    'cpu'          : "max",
    'machine'      : "virt,gic-version=max",
}

# The minimum minor version of QEMU we will support with aarch64 VMs is 3.
# QEMU versions less than 3 have various issues running these VMs.
QEMU_AARCH64_MIN_VERSION = 3

# The DEFAULT_CONFIG will default to a version of
# parameters that works for backwards compatibility.
DEFAULT_CONFIG = {'kvm' : {'cpu'          : "host",
                           'machine'      : "virt,gic-version=host"},
                  'tcg' : {'cpu'          : "cortex-a57",
                           'machine'      : "virt"},
}

def get_config_defaults(vmcls, default_config):
    """Fetch the configuration defaults for this VM,
       taking into consideration the defaults for
       aarch64 first, followed by the defaults for this VM."""
    config = default_config
    config.update(aarch_get_config_defaults(vmcls))
    return config

def aarch_get_config_defaults(vmcls):
    """Set the defaults for current version of QEMU."""
    config = CURRENT_CONFIG
    args = basevm.parse_args(vmcls)
    qemu_path = basevm.get_qemu_path(vmcls.arch, args.build_path)
    qemu_version = basevm.get_qemu_version(qemu_path)
    if qemu_version < QEMU_AARCH64_MIN_VERSION:
        error = "\nThis major version of QEMU {} is to old for aarch64 VMs.\n"\
                "The major version must be at least {}.\n"\
                "To continue with the current build of QEMU, "\
                "please restart with QEMU_LOCAL=1 .\n"
        print(error.format(qemu_version, QEMU_AARCH64_MIN_VERSION))
        exit(1)
    if qemu_version == QEMU_AARCH64_MIN_VERSION:
        # We have an older version of QEMU,
        # set the config values for backwards compatibility.
        if kvm_available('aarch64'):
            config.update(DEFAULT_CONFIG['kvm'])
        else:
            config.update(DEFAULT_CONFIG['tcg'])
    return config

def create_flash_images(flash_dir="./", efi_img=""):
    """Creates the appropriate pflash files
       for an aarch64 VM."""
    flash0_path = get_flash_path(flash_dir, "flash0")
    flash1_path = get_flash_path(flash_dir, "flash1")
    fd_null = open(os.devnull, 'w')
    subprocess.check_call(["dd", "if=/dev/zero", "of={}".format(flash0_path),
                           "bs=1M", "count=64"],
                           stdout=fd_null, stderr=subprocess.STDOUT)
    # A reliable way to get the QEMU EFI image is via an installed package or
    # via the bios included with qemu.
    if not os.path.exists(efi_img):
        sys.stderr.write("*** efi argument is invalid ({})\n".format(efi_img))
        sys.stderr.write("*** please check --efi-aarch64 argument or "\
                         "install qemu-efi-aarch64 package\n")
        exit(3)
    subprocess.check_call(["dd", "if={}".format(efi_img),
                           "of={}".format(flash0_path),
                           "conv=notrunc"],
                           stdout=fd_null, stderr=subprocess.STDOUT)
    subprocess.check_call(["dd", "if=/dev/zero",
                           "of={}".format(flash1_path),
                           "bs=1M", "count=64"],
                           stdout=fd_null, stderr=subprocess.STDOUT)
    fd_null.close()

def get_pflash_args(flash_dir="./"):
    """Returns a string that can be used to
       boot qemu using the appropriate pflash files
       for aarch64."""
    flash0_path = get_flash_path(flash_dir, "flash0")
    flash1_path = get_flash_path(flash_dir, "flash1")
    pflash_args_str = "-drive file={},format=raw,if=pflash "\
                      "-drive file={},format=raw,if=pflash"
    pflash_args = pflash_args_str.format(flash0_path, flash1_path)
    return pflash_args.split(" ")

def get_flash_path(flash_dir, name):
    return os.path.join(flash_dir, "{}.img".format(name))
