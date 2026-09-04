#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [ $# -lt 3 ]; then
    echo "check_plugin_output: check_cmd qemu_bin [args]..." 1>&2
    exit 1
fi
check_cmd="$1"; shift
check_cmd_args=""
while [[ "$1" == "-"* ]]; do
    check_cmd_args="$check_cmd_args $1"
    shift
done

qemu="$1"; shift
tmp=$(mktemp -d)
trap "rm -rf $tmp" EXIT

set -x

$qemu -d plugin -D $tmp/plugin "$@" |& tee $tmp/output
cat $tmp/plugin
$check_cmd $check_cmd_args $tmp/output $tmp/plugin
