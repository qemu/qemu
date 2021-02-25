#! /bin/sh
#
# Fix multiline comments to match docs/devel/style.rst
#
# Copyright (C) 2018 Red Hat, Inc.
#
# Author: Paolo Bonzini
#
# Usage: scripts/fix-multiline-comments.sh [-i] FILE...
#
# -i edits the file in place (requires gawk 4.1.0).
#
# Set the AWK environment variable to choose the awk interpreter to use
# (default 'awk')

if test "$1" = -i; then
  # gawk extension
  inplace="-i inplace"
  shift
fi
${AWK-awk} $inplace 'BEGIN { indent = -1 }
{
    line = $0
    # apply a star to the indent on lines after the first
    if (indent != -1) {
        if (line == "") {
            line = sp " *"
        } else if (substr(line, 1, indent + 2) == sp "  ") {
            line = sp " *" substr(line, indent + 3)
        }
    }

    is_lead = (line ~ /^[ \t]*\/\*/)
    is_trail = (line ~ /\*\//)
    if (is_lead && !is_trail) {
        # grab the indent at the start of a comment, but not for
        # single-line comments
        match(line, /^[ \t]*\/\*/)
        indent = RLENGTH - 2
        sp = substr(line, 1, indent)
    }

    # the regular expression filters out lone /*, /**, or */
    if (indent != -1 && !(line ~ /^[ \t]*(\/\*+|\*\/)[ \t]*$/)) {
        if (is_lead) {
            # split the leading /* or /** on a separate line
            match(line, /^[ \t]*\/\*+/)
            lead = substr(line, 1, RLENGTH)
            match(line, /^[ \t]*\/\*+[ \t]*/)
            line = lead "\n" sp " *" substr(line, RLENGTH)
        }
        if (is_trail) {
            # split the trailing */ on a separate line
            match(line, /[ \t]*\*\//)
            line = substr(line, 1, RSTART - 1) "\n" sp " */"
        }
    }
    if (is_trail) {
        indent = -1
    }
    print line
}' "$@"
