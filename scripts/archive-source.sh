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
list_file="${tar_file}.list"
vroot_dir="${tar_file}.vroot"

# We want a predictable list of submodules for builds, that is
# independent of what the developer currently has initialized
# in their checkout, because the build environment is completely
# different to the host OS.
submodules="dtc ui/keycodemapdb"

trap "status=$?; rm -rf \"$list_file\" \"$vroot_dir\"; exit \$status" 0 1 2 3 15

if git diff-index --quiet HEAD -- &>/dev/null
then
    HEAD=HEAD
else
    HEAD=$(git stash create)
fi
git clone --shared . "$vroot_dir"
test $? -ne 0 && error "failed to clone into '$vroot_dir'"

cd "$vroot_dir"
test $? -ne 0 && error "failed to change into '$vroot_dir'"

git checkout $HEAD
test $? -ne 0 && error "failed to checkout $HEAD revision"

for sm in $submodules; do
    git submodule update --init $sm
    test $? -ne 0 && error "failed to init submodule $sm"
done

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
