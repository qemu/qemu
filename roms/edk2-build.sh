#!/bin/bash

# Wrapper shell script for building a  virtual platform firmware in edk2.
#
# Copyright (C) 2019 Red Hat, Inc.
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
emulation_target=$1
shift
num_args=0
args=()
for arg in "$@"; do
  args[num_args++]="$arg"
done
shift $num_args

cd edk2

export PYTHON_COMMAND=${EDK2_PYTHON_COMMAND:-python3}

# Source "edksetup.sh" carefully.
set +e +u +C
source ./edksetup.sh
ret=$?
set -e -u -C
if [ $ret -ne 0 ]; then
  exit $ret
fi

# Fetch some option arguments, and set the cross-compilation environment (if
# any), for the edk2 "build" utility.
source ../edk2-funcs.sh
edk2_toolchain=$(qemu_edk2_get_toolchain "$emulation_target")
MAKEFLAGS=$(qemu_edk2_quirk_tianocore_1607 "$MAKEFLAGS")
edk2_thread_count=$(qemu_edk2_get_thread_count "$MAKEFLAGS")
qemu_edk2_set_cross_env "$emulation_target"

# Build the platform firmware.
build \
  --cmd-len=65536 \
  -n "$edk2_thread_count" \
  --buildtarget=DEBUG \
  --tagname="$edk2_toolchain" \
  "${args[@]}"
