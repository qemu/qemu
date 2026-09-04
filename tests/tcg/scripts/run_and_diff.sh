#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "run_and_diff: expected_output_file cmd [args]..." 1>&2
    exit 1
fi
expected="$1";shift
output=$(mktemp)
trap "rm -rf $output" EXIT
set -x
"$@" |& tee $output
diff $output $expected
