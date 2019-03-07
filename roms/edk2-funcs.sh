# Shell script that defines functions for determining some environmental
# characteristics for the edk2 "build" utility.
#
# This script is meant to be sourced, in a bash environment.
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


# Verify whether the QEMU system emulation target is supported by the UEFI spec
# and edk2. Print a message to the standard error, and return with nonzero
# status, if verification fails.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_verify_arch()
{
  local emulation_target="$1"
  local program_name=$(basename -- "$0")

  case "$emulation_target" in
    (arm|aarch64|i386|x86_64)
      ;;
    (*)
      printf '%s: unknown/unsupported QEMU system emulation target "%s"\n' \
        "$program_name" "$emulation_target" >&2
      return 1
      ;;
  esac
}


# Translate the QEMU system emulation target to the edk2 architecture
# identifier. Print the result to the standard output.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_get_arch()
{
  local emulation_target="$1"

  if ! qemu_edk2_verify_arch "$emulation_target"; then
    return 1
  fi

  case "$emulation_target" in
    (arm)
      printf 'ARM\n'
      ;;
    (aarch64)
      printf 'AARCH64\n'
      ;;
    (i386)
      printf 'IA32\n'
      ;;
    (x86_64)
      printf 'X64\n'
      ;;
  esac
}


# Translate the QEMU system emulation target to the gcc cross-compilation
# architecture identifier. Print the result to the standard output.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_get_gcc_arch()
{
  local emulation_target="$1"

  if ! qemu_edk2_verify_arch "$emulation_target"; then
    return 1
  fi

  case "$emulation_target" in
    (arm|aarch64|x86_64)
      printf '%s\n' "$emulation_target"
      ;;
    (i386)
      printf 'i686\n'
      ;;
  esac
}


# Determine the gcc cross-compiler prefix (if any) for use with the edk2
# toolchain. Print the result to the standard output.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_get_cross_prefix()
{
  local emulation_target="$1"
  local gcc_arch
  local host_arch

  if ! gcc_arch=$(qemu_edk2_get_gcc_arch "$emulation_target"); then
    return 1
  fi

  host_arch=$(uname -m)

  if [ "$gcc_arch" == "$host_arch" ] ||
     ( [ "$gcc_arch" == i686 ] && [ "$host_arch" == x86_64 ] ); then
    # no cross-compiler needed
    :
  else
    printf '%s-linux-gnu-\n' "$gcc_arch"
  fi
}


# Determine the edk2 toolchain tag for the QEMU system emulation target. Print
# the result to the standard output. Print a message to the standard error, and
# return with nonzero status, if the (conditional) gcc version check fails.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_get_toolchain()
{
  local emulation_target="$1"
  local program_name=$(basename -- "$0")
  local cross_prefix
  local gcc_version

  if ! qemu_edk2_verify_arch "$emulation_target"; then
    return 1
  fi

  case "$emulation_target" in
    (arm|aarch64)
      printf 'GCC5\n'
      ;;

    (i386|x86_64)
      if ! cross_prefix=$(qemu_edk2_get_cross_prefix "$emulation_target"); then
        return 1
      fi

      gcc_version=$("${cross_prefix}gcc" -v 2>&1 | tail -1 | awk '{print $3}')
      # Run "git-blame" on "OvmfPkg/build.sh" in edk2 for more information on
      # the mapping below.
      case "$gcc_version" in
        ([1-3].*|4.[0-7].*)
          printf '%s: unsupported gcc version "%s"\n' \
            "$program_name" "$gcc_version" >&2
          return 1
          ;;
        (4.8.*)
          printf 'GCC48\n'
          ;;
        (4.9.*|6.[0-2].*)
          printf 'GCC49\n'
          ;;
        (*)
          printf 'GCC5\n'
          ;;
      esac
      ;;
  esac
}


# Determine the name of the environment variable that exposes the
# cross-compiler prefix to the edk2 "build" utility. Print the result to the
# standard output.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_get_cross_prefix_var()
{
  local emulation_target="$1"
  local edk2_toolchain
  local edk2_arch

  if ! edk2_toolchain=$(qemu_edk2_get_toolchain "$emulation_target"); then
    return 1
  fi

  case "$emulation_target" in
    (arm|aarch64)
      if ! edk2_arch=$(qemu_edk2_get_arch "$emulation_target"); then
        return 1
      fi
      printf '%s_%s_PREFIX\n' "$edk2_toolchain" "$edk2_arch"
      ;;
    (i386|x86_64)
      printf '%s_BIN\n' "$edk2_toolchain"
      ;;
  esac
}


# Set and export the environment variable(s) necessary for cross-compilation,
# whenever needed by the edk2 "build" utility.
#
# Parameters:
#   $1: QEMU system emulation target
qemu_edk2_set_cross_env()
{
  local emulation_target="$1"
  local cross_prefix
  local cross_prefix_var

  if ! cross_prefix=$(qemu_edk2_get_cross_prefix "$emulation_target"); then
    return 1
  fi

  if [ -z "$cross_prefix" ]; then
    # Nothing to do.
    return 0
  fi

  if ! cross_prefix_var=$(qemu_edk2_get_cross_prefix_var \
                            "$emulation_target"); then
    return 1
  fi

  eval "export $cross_prefix_var=\$cross_prefix"
}


# Determine the "-n" option argument (that is, the number of modules to build
# in parallel) for the edk2 "build" utility. Print the result to the standard
# output.
#
# Parameters:
#   $1: the value of the MAKEFLAGS variable
qemu_edk2_get_thread_count()
{
  local makeflags="$1"

  if [[ "$makeflags" == *--jobserver-auth=* ]] ||
     [[ "$makeflags" == *--jobserver-fds=* ]]; then
    # If there is a job server, allow the edk2 "build" utility to parallelize
    # as many module builds as there are logical CPUs in the system. The "make"
    # instances forked by "build" are supposed to limit themselves through the
    # job server. The zero value below causes the edk2 "build" utility to fetch
    # the logical CPU count with Python's multiprocessing.cpu_count() method.
    printf '0\n'
  else
    # Build a single module at a time.
    printf '1\n'
  fi
}
