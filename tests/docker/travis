#!/bin/bash -e
#
# Mimic a travis testing matrix
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

requires pyyaml
cmdfile=/tmp/travis_cmd_list.sh
$QEMU_SRC/tests/docker/travis.py $QEMU_SRC/.travis.yml > $cmdfile
chmod +x $cmdfile
cd "$QEMU_SRC"
unset BUILD_DIR SRC_DIR
$cmdfile
