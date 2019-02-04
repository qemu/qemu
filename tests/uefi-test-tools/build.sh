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

# Source "edksetup.sh" carefully.
set +e +u +C
source "$PACKAGES_PATH/edksetup.sh"
ret=$?
set -e -u -C
if [ $ret -ne 0 ]; then
  exit $ret
fi

# Map the QEMU system emulation target to the following types of architecture
# identifiers:
# - edk2,
# - gcc cross-compilation.
# Cover only those targets that are supported by the UEFI spec and edk2.
case "$emulation_target" in
  (arm)
    edk2_arch=ARM
    gcc_arch=arm
    ;;
  (aarch64)
    edk2_arch=AARCH64
    gcc_arch=aarch64
    ;;
  (i386)
    edk2_arch=IA32
    gcc_arch=i686
    ;;
  (x86_64)
    edk2_arch=X64
    gcc_arch=x86_64
    ;;
  (*)
    printf '%s: unknown/unsupported QEMU system emulation target "%s"\n' \
      "$program_name" "$emulation_target" >&2
    exit 1
    ;;
esac

# Check if cross-compilation is needed.
host_arch=$(uname -m)
if [ "$gcc_arch" == "$host_arch" ] ||
   ( [ "$gcc_arch" == i686 ] && [ "$host_arch" == x86_64 ] ); then
  cross_prefix=
else
  cross_prefix=${gcc_arch}-linux-gnu-
fi

# Expose cross_prefix (which is possibly empty) to the edk2 tools. While at it,
# determine the suitable edk2 toolchain as well.
# - For ARM and AARCH64, edk2 only offers the GCC5 toolchain tag, which covers
#   the gcc-5+ releases.
# - For IA32 and X64, edk2 offers the GCC44 through GCC49 toolchain tags, in
#   addition to GCC5. Unfortunately, the mapping between the toolchain tags and
#   the actual gcc releases isn't entirely trivial. Run "git-blame" on
#   "OvmfPkg/build.sh" in edk2 for more information.
# And, because the above is too simple, we have to assign cross_prefix to an
# edk2 build variable that is specific to both the toolchain tag and the target
# architecture.
case "$edk2_arch" in
  (ARM)
    edk2_toolchain=GCC5
    export GCC5_ARM_PREFIX=$cross_prefix
    ;;
  (AARCH64)
    edk2_toolchain=GCC5
    export GCC5_AARCH64_PREFIX=$cross_prefix
    ;;
  (IA32|X64)
    gcc_version=$("${cross_prefix}gcc" -v 2>&1 | tail -1 | awk '{print $3}')
    case "$gcc_version" in
      ([1-3].*|4.[0-3].*)
        printf '%s: unsupported gcc version "%s"\n' \
          "$program_name" "$gcc_version" >&2
        exit 1
        ;;
      (4.4.*)
        edk2_toolchain=GCC44
        ;;
      (4.5.*)
        edk2_toolchain=GCC45
        ;;
      (4.6.*)
        edk2_toolchain=GCC46
        ;;
      (4.7.*)
        edk2_toolchain=GCC47
        ;;
      (4.8.*)
        edk2_toolchain=GCC48
        ;;
      (4.9.*|6.[0-2].*)
        edk2_toolchain=GCC49
        ;;
      (*)
        edk2_toolchain=GCC5
        ;;
    esac
    eval "export ${edk2_toolchain}_BIN=\$cross_prefix"
    ;;
esac

# Build the UEFI binary
mkdir -p log
build \
  --arch="$edk2_arch" \
  --buildtarget=DEBUG \
  --platform=UefiTestToolsPkg/UefiTestToolsPkg.dsc \
  --tagname="$edk2_toolchain" \
  --module="UefiTestToolsPkg/$dsc_component/$dsc_component.inf" \
  --log="log/$dsc_component.$edk2_arch.log" \
  --report-file="log/$dsc_component.$edk2_arch.report"
cp -a -- \
  "Build/UefiTestTools/DEBUG_${edk2_toolchain}/$edk2_arch/$dsc_component.efi" \
  "$uefi_binary"
