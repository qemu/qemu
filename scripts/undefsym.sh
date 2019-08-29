#! /usr/bin/env bash

# Before a shared module's DSO is produced, a static library is built for it
# and passed to this script.  The script generates -Wl,-u options to force
# the inclusion of symbol from libqemuutil.a if the shared modules need them,
# This is necessary because the modules may use functions not needed by the
# executable itself, which would cause the function to not be linked in.
# Then the DSO loading would fail because of the missing symbol.

if test $# -le 2; then
  exit 0
fi

NM=$1
staticlib=$2
shift 2
# Find symbols defined in static libraries and undefined in shared modules
comm -12 \
  <( $NM -P -g $staticlib | awk '$2!="U"{print "-Wl,-u," $1}' | sort -u) \
  <( $NM -P -g "$@" | awk '$2=="U"{print "-Wl,-u," $1}' | sort -u)
