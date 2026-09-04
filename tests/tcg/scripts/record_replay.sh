#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "record_replay: qemu_bin [args]..." 1>&2
    exit 1
fi
qemu="$1";shift
tmp=$(mktemp -d)
trap "rm -rf $tmp" EXIT
set -x
$qemu -icount shift=5,rr=record,rrfile=$tmp/rr.bin "$@" |& tee $tmp/record
$qemu -icount shift=5,rr=replay,rrfile=$tmp/rr.bin "$@" |& tee $tmp/replay
diff $tmp/record $tmp/replay
