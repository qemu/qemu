#!/bin/bash

# Build script that determines the edk2 toolchain to use, invokes the edk2
# "build" utility, and copies the built UEFI binary to the requested location.
#
# Copyright (C) 2019, Red Hat, Inc.
#
# This program and the accompanying materials are licensed and made available
# under the terms and conditions of the BSD License that accompanies this
# distribution. The full text of the license may be found at
# <http://opensource.org/licenses/bsd-license.php>.
#
# THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
# WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

set -e -u -C

# Save the command line arguments. We need to reset $# to 0 before sourcing
# "edksetup.sh", as it will inherit $@.
program_name=$(basename -- "$0")
edk2_dir=$1
dsc_component=$2
emulation_target=$3
uefi_binary=$4
shift 4

# Set up the environment for edk2 building.
export PACKAGES_PATH=$(realpath -- "$edk2_dir")
export WORKSPACE=$PWD
mkdir -p Conf

export PYTHON_COMMAND=${EDK2_PYTHON_COMMAND:-python3}

# Source "edksetup.sh" carefully.
set +e +u +C
source "$PACKAGES_PATH/edksetup.sh"
ret=$?
set -e -u -C
if [ $ret -ne 0 ]; then
  exit $ret
fi

# Fetch some option arguments, and set the cross-compilation environment (if
# any), for the edk2 "build" utility.
source "$edk2_dir/../edk2-funcs.sh"
edk2_arch=$(qemu_edk2_get_arch "$emulation_target")
edk2_toolchain=$(qemu_edk2_get_toolchain "$emulation_target")
MAKEFLAGS=$(qemu_edk2_quirk_tianocore_1607 "$MAKEFLAGS")
edk2_thread_count=$(qemu_edk2_get_thread_count "$MAKEFLAGS")
qemu_edk2_set_cross_env "$emulation_target"

# Build the UEFI binary
mkdir -p log
build \
  --arch="$edk2_arch" \
  -n "$edk2_thread_count" \
  --buildtarget=DEBUG \
  --platform=UefiTestToolsPkg/UefiTestToolsPkg.dsc \
  --tagname="$edk2_toolchain" \
  --module="UefiTestToolsPkg/$dsc_component/$dsc_component.inf" \
  --log="log/$dsc_component.$edk2_arch.log" \
  --report-file="log/$dsc_component.$edk2_arch.report"
cp -a -- \
  "Build/UefiTestTools/DEBUG_${edk2_toolchain}/$edk2_arch/$dsc_component.efi" \
  "$uefi_binary"
