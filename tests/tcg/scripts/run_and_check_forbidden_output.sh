#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "run_and_check_forbidden_output: 'forbidden string' cmd [args]..." 1>&2
    exit 1
fi
forbidden="$1";shift
output=$("$@" 2>&1)
set -x
if ! echo "$output" | grep -Fi "$forbidden"
then
    echo "output does not contain forbidden string: $forbidden"
    exit 0
fi
echo "found forbidden string: $forbidden" 1>&2
exit 1
