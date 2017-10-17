#!/bin/sh
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

set -e

substat=".git-submodule-status"

command=$1
shift
modules="$@"

if test -z "$modules"
then
    test -e $substat || touch $substat
    exit 0
fi

if ! test -e ".git"
then
    echo "$0: unexpectedly called with submodules but no git checkout exists"
    exit 1
fi

case "$command" in
status)
    test -f "$substat" || exit 1
    trap "rm -f ${substat}.tmp" EXIT
    git submodule status $modules > "${substat}.tmp"
    diff "${substat}" "${substat}.tmp" >/dev/null
    exit $?
    ;;
update)
    git submodule update --init $modules 1>/dev/null 2>&1
    git submodule status $modules > "${substat}"
    ;;
esac
