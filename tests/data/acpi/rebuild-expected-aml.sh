#!/usr/bin/env bash

#
# Rebuild expected AML files for acpi unit-test
#
# Copyright (c) 2013 Red Hat Inc.
#
# Authors:
#  Marcel Apfelbaum <marcel.a@redhat.com>
#  Igor Mammedov <imammedo@redhat.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

qemu_arches="x86_64 aarch64 riscv64 loongarch64"

if [ ! -e "tests/qtest/bios-tables-test" ]; then
    echo "Test: bios-tables-test is required! Run make check before this script."
    echo "Run this script from the build directory."
    exit 1;
fi

if grep TARGET_DIRS= config-host.mak; then
    for arch in $qemu_arches; do
        if  grep TARGET_DIRS= config-host.mak | grep "$arch"-softmmu;
        then
            qemu_bins="$qemu_bins ./qemu-system-$arch"
        fi
    done
else
    echo "config-host.mak missing!"
    echo "Run this script from the build directory."
    exit 1;
fi

if [ -z "$qemu_bins" ]; then
    echo "Only the following architectures are currently supported: $qemu_arches"
    echo "None of these configured!"
    echo "To fix, run configure \
         --target-list=x86_64-softmmu,aarch64-softmmu,riscv64-softmmu,loongarch64-softmmu"
    exit 1;
fi

for qemu in $qemu_bins; do
    if [ ! -e $qemu ]; then
        echo "Run 'make' to build the following QEMU executables: $qemu_bins"
        echo "Also, run this script from the build directory."
        exit 1;
    fi
    TEST_ACPI_REBUILD_AML=y QTEST_QEMU_BINARY=$qemu tests/qtest/bios-tables-test
done

eval `grep SRC_PATH= config-host.mak`

old_allowed_dif=`grep -v -e 'List of comma-separated changed AML files to ignore' ${SRC_PATH}/tests/qtest/bios-tables-test-allowed-diff.h`

echo '/* List of comma-separated changed AML files to ignore */' > ${SRC_PATH}/tests/qtest/bios-tables-test-allowed-diff.h

echo "The files were rebuilt and can be added to git."
echo "You can use ${SRC_PATH}/tests/data/acpi/disassemle-aml.sh to disassemble them to ASL."

if [ -z "$old_allowed_dif" ]; then
    echo "Note! Please do not commit expected files with source changes"
    echo "Note! Please follow the process documented in ${SRC_PATH}/tests/qtest/bios-tables-test.c"
fi
