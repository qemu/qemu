#!/bin/bash
#
# Author: Fam Zheng <famz@redhat.com>
#
# Archive source tree, including submodules. This is created for test code to
# export the source files, in order to be built in a different environment,
# such as in a docker instance or VM.
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

error() {
    printf %s\\n "$*" >&2
    exit 1
}

if test $# -lt 1; then
    error "Usage: $0 <output tarball>"
fi

tar_file="$1"
list_file="$1.list"
submodules=$(git submodule foreach --recursive --quiet 'echo $name')

if test $? -ne 0; then
    error "git submodule command failed"
fi

trap "status=$?; rm -f \"$list_file\"; exit \$status" 0 1 2 3 15

if test -n "$submodules"; then
    {
        git ls-files || error "git ls-files failed"
        for sm in $submodules; do
            (cd $sm; git ls-files) | sed "s:^:$sm/:"
            if test "${PIPESTATUS[*]}" != "0 0"; then
                error "git ls-files in submodule $sm failed"
            fi
        done
    } | grep -x -v $(for sm in $submodules; do echo "-e $sm"; done) > "$list_file"
else
    git ls-files > "$list_file"
fi

if test $? -ne 0; then
    error "failed to generate list file"
fi

tar -cf "$tar_file" -T "$list_file" || error "failed to create tar file"

exit 0
