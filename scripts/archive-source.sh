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

tar_file=$(realpath "$1")
sub_tdir=$(mktemp -d "${tar_file%.tar}.sub.XXXXXXXX")
sub_file="${sub_tdir}/submodule.tar"

# We want a predictable list of submodules for builds, that is
# independent of what the developer currently has initialized
# in their checkout, because the build environment is completely
# different to the host OS.
submodules="dtc slirp ui/keycodemapdb tests/fp/berkeley-softfloat-3 tests/fp/berkeley-testfloat-3"
sub_deinit=""

function cleanup() {
    local status=$?
    rm -rf "$sub_tdir"
    if test "$sub_deinit" != ""; then
        git submodule deinit $sub_deinit
    fi
    exit $status
}
trap "cleanup" 0 1 2 3 15

if git diff-index --quiet HEAD -- &>/dev/null
then
    HEAD=HEAD
else
    HEAD=$(git stash create)
fi

git archive --format tar $HEAD > "$tar_file"
test $? -ne 0 && error "failed to archive qemu"
for sm in $submodules; do
    status="$(git submodule status "$sm")"
    smhash="${status#[ +-]}"
    smhash="${smhash%% *}"
    case "$status" in
        -*)
            sub_deinit="$sub_deinit $sm"
            git submodule update --init "$sm"
            test $? -ne 0 && error "failed to update submodule $sm"
            ;;
        +*)
            echo "WARNING: submodule $sm is out of sync"
            ;;
    esac
    (cd $sm; git archive --format tar --prefix "$sm/" $smhash) > "$sub_file"
    test $? -ne 0 && error "failed to archive submodule $sm ($smhash)"
    tar --concatenate --file "$tar_file" "$sub_file"
    test $? -ne 0 && error "failed append submodule $sm to $tar_file"
done
exit 0
