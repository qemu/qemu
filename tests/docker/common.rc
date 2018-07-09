#!/bin/sh
#
# Common routines for docker test scripts.
#
# Copyright (c) 2016 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or (at your option) any later version. See the COPYING file in
# the top-level directory.

requires()
{
    for c in $@; do
        if ! echo "$FEATURES" | grep -wq -e "$c"; then
            echo "Prerequisite '$c' not present, skip"
            exit 0
        fi
    done
}

configure_qemu()
{
    config_opts="--enable-werror \
                 ${TARGET_LIST:+--target-list=${TARGET_LIST}} \
                 --prefix=$INSTALL_DIR \
                 $QEMU_CONFIGURE_OPTS $EXTRA_CONFIGURE_OPTS \
                 $@"
    echo "Configure options:"
    echo $config_opts
    $QEMU_SRC/configure $config_opts || \
        { cat config.log && test_fail "Failed to run 'configure'"; }
}

build_qemu()
{
    configure_qemu $@
    make $MAKEFLAGS
}

check_qemu()
{
    # default to make check unless the caller specifies
    if test -z "$@"; then
        INVOCATION="check"
    else
        INVOCATION="$@"
    fi

    if command -v gtester > /dev/null 2>&1 && \
           gtester --version > /dev/null 2>&1; then
        make $MAKEFLAGS $INVOCATION
    else
        echo "No working gtester, skipping make $INVOCATION"
    fi
}

test_fail()
{
    echo "$@"
    exit 1
}

prep_fail()
{
    echo "$@"
    exit 2
}

install_qemu()
{
    make install $MAKEFLAGS DESTDIR=$PWD/=destdir
    ret=$?
    rm -rf $PWD/=destdir
    return $ret
}
