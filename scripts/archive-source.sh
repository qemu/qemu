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
subprojects="keycodemapdb libvfio-user berkeley-softfloat-3
  berkeley-testfloat-3 arbitrary-int-1-rs bilge-0.2-rs
  bilge-impl-0.2-rs either-1-rs itertools-0.11-rs libc-0.2-rs proc-macro2-1-rs
  proc-macro-error-1-rs proc-macro-error-attr-1-rs quote-1-rs
  syn-2-rs unicode-ident-1-rs"
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

function tree_ish() {
    local retval='HEAD'
    if ! git diff-index --quiet --ignore-submodules=all HEAD -- &>/dev/null
    then
        retval=$(git stash create)
    fi
    echo "$retval"
}

function subproject_dir() {
    if test ! -f "subprojects/$1.wrap"; then
      error "scripts/archive-source.sh should only process wrap subprojects"
    fi

    # Print the directory key of the wrap file, defaulting to the
    # subproject name.  The wrap file is in ini format and should
    # have a single section only.  There should be only one section
    # named "[wrap-*]", which helps keeping the script simple.
    local dir
    dir=$(sed -n \
      -e '/^\[wrap-[a-z][a-z]*\]$/,/^\[/{' \
      -e    '/^directory *= */!b' \
      -e    's///p' \
      -e    'q' \
      -e '}' \
      "subprojects/$1.wrap")

    echo "${dir:-$1}"
}

git archive --format tar "$(tree_ish)" > "$tar_file"
test $? -ne 0 && error "failed to archive qemu"

for sp in $subprojects; do
    meson subprojects download $sp
    test $? -ne 0 && error "failed to download subproject $sp"
    tar --append --file "$tar_file" --exclude=.git subprojects/"$(subproject_dir $sp)"
    test $? -ne 0 && error "failed to append subproject $sp to $tar_file"
done
exit 0
