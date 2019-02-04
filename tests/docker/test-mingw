#!/bin/bash -e
#
# Cross compile QEMU with mingw toolchain on Linux.
#
# Copyright (c) 2016 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or (at your option) any later version. See the COPYING file in
# the top-level directory.

. common.rc

requires mingw dtc

cd "$BUILD_DIR"
DEF_TARGET_LIST="x86_64-softmmu,aarch64-softmmu"

for prefix in x86_64-w64-mingw32- i686-w64-mingw32-; do
    TARGET_LIST=${TARGET_LIST:-$DEF_TARGET_LIST} \
        build_qemu --cross-prefix=$prefix \
        --enable-trace-backends=simple \
        --enable-gnutls \
        --enable-nettle \
        --enable-curl \
        --enable-vnc \
        --enable-bzip2 \
        --enable-guest-agent
    install_qemu
    make clean

done
