#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "run_with_input: input_string cmd [args]..." 1>&2
    exit 1
fi
input="$1";shift
set -x
echo "$input" | "$@"
