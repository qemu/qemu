#!/usr/bin/env bash

# This script checks that plugin_output contains line from test_output.
# Each line of output is searched (as a regexp) in plugin_output.

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

[ $# -eq 2 ] || die "usage: test_output plugin_out"

test_output=$1;shift
plugin_output=$1;shift

cat $test_output | while read line; do
    check "$plugin_output" "$line"
done
