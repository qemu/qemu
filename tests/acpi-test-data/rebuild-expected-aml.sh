#! /bin/bash

#
# Rebuild expected AML files for acpi unit-test
#
# Copyright (c) 2013 Red Hat Inc.
#
# Authors:
#  Marcel Apfelbaum <marcel.a@redhat.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

qemu=

if [ -e x86_64-softmmu/qemu-system-x86_64 ]; then
    qemu="x86_64-softmmu/qemu-system-x86_64"
elif [ -e i386-softmmu/qemu-system-i386 ]; then
    qemu="i386-softmmu/qemu-system-i386"
else
    echo "Run 'make' to build the qemu exectutable!"
    echo "Run this script from the build directory."
    exit 1;
fi

if [ ! -e "tests/bios-tables-test" ]; then
    echo "Test: bios-tables-test is required! Run make check before this script."
    echo "Run this script from the build directory."
    exit 1;
fi

TEST_ACPI_REBUILD_AML=y QTEST_QEMU_BINARY=$qemu tests/bios-tables-test

echo "The files were rebuilt and can be added to git."
echo "However, if new files were created, please copy them manually" \
     "to tests/acpi-test-data/pc/ or tests/acpi-test-data/q35/ ."
