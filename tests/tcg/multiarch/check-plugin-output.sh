#!/usr/bin/env bash

# This script runs a given executable using qemu, and compare its standard
# output with an expected plugin output.
# Each line of output is searched (as a regexp) in the expected plugin output.

set -euo pipefail

die()
{
    echo "$@" 1>&2
    exit 1
}

check()
{
    file=$1
    pattern=$2
    grep "$pattern" "$file" > /dev/null || die "\"$pattern\" not found in $file"
}

[ $# -eq 3 ] || die "usage: qemu_bin exe plugin_out_file"

qemu_bin=$1; shift
exe=$1;shift
plugin_out=$1; shift

expected()
{
    $qemu_bin $exe ||
        die "running $exe failed"
}

expected | while read line; do
    check "$plugin_out" "$line"
done
