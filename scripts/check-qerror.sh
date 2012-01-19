#!/bin/sh
# This script verifies that qerror definitions and table entries are
# alphabetically ordered.

check_order() {
  errmsg=$1
  shift

  # sort -C verifies order but does not print a message.  sort -c does print a
  # message.  These options are both in POSIX.
  if ! "$@" | sort -C; then
    echo "$errmsg"
    "$@" | sort -c
    exit 1
  fi
  return 0
}

check_order 'Definitions in qerror.h must be in alphabetical order:' \
            grep '^#define QERR_' qerror.h
check_order 'Entries in qerror.c:qerror_table must be in alphabetical order:' \
            sed -n '/^static.*qerror_table\[\]/,/^};/s/QERR_/&/gp' qerror.c
