#!/bin/bash
#
# Run block test cases
#
# Copyright 2017 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

. ./common.rc

cd "$BUILD_DIR"

build_qemu --target-list=x86_64-softmmu || test_fail "Build failed"
cd tests/qemu-iotests
for t in raw qcow2 nbd luks; do
    ./check -g quick -$t || test_fail "Test failed: iotests $t"
done
