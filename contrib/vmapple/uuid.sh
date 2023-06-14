#!/bin/sh
#
# Used for converting a guest provisioned using Virtualization.framework
# for use with the QEMU 'vmapple' aarch64 machine type.
#
# Extracts the Machine UUID from Virtualization.framework VM JSON file.
# (as produced by 'macosvm', passed as command line argument)
#
# SPDX-License-Identifier: GPL-2.0-or-later

plutil -extract machineId raw "$1" | base64 -d | plutil -extract ECID raw -

