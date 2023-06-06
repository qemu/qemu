#!/bin/sh
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

substat=".git-submodule-status"

command=$1
shift
maybe_modules="$@"

# if not running in a git checkout, do nothing
test "$command" = "ignore" && exit 0

test -z "$GIT" && GIT=$(command -v git)

cd "$(dirname "$0")/.."

update_error() {
    echo "$0: $*"
    echo
    echo "Unable to automatically checkout GIT submodules '$modules'."
    echo "If you require use of an alternative GIT binary (for example to"
    echo "enable use of a transparent proxy), please disable automatic"
    echo "GIT submodule checkout with:"
    echo
    echo " $ ./configure --disable-download"
    echo
    echo "and then manually update submodules prior to running make, with:"
    echo
    echo " $ GIT='tsocks git' scripts/git-submodule.sh update $modules"
    echo
    exit 1
}

validate_error() {
    if test "$1" = "validate"; then
        echo "GIT submodules checkout is out of date, and submodules"
        echo "configured for validate only. Please run"
        echo "  scripts/git-submodule.sh update $maybe_modules"
        echo "from the source directory or call configure with"
        echo "  --enable-download"
    fi
    exit 1
}

check_updated() {
    local CURSTATUS OLDSTATUS
    CURSTATUS=$($GIT submodule status $module)
    OLDSTATUS=$(grep $module $substat)
    test "$CURSTATUS" = "$OLDSTATUS"
}

if test -n "$maybe_modules" && ! test -e ".git"
then
    echo "$0: unexpectedly called with submodules but no git checkout exists"
    exit 1
fi

if test -n "$maybe_modules" && test -z "$GIT"
then
    echo "$0: unexpectedly called with submodules but git binary not found"
    exit 1
fi

modules=""
for m in $maybe_modules
do
    $GIT submodule status $m 1> /dev/null 2>&1
    if test $? = 0
    then
        modules="$modules $m"
    else
        echo "warn: ignoring non-existent submodule $m"
    fi
done

case "$command" in
status|validate)
    test -f "$substat" || validate_error "$command"
    test -z "$maybe_modules" && exit 0
    for module in $modules; do
        check_updated $module || validate_error "$command"
    done
    exit 0
    ;;
update)
    test -e $substat || touch $substat
    test -z "$maybe_modules" && exit 0

    $GIT submodule update --init $modules 1>/dev/null
    test $? -ne 0 && update_error "failed to update modules"
    for module in $modules; do
        check_updated $module || echo Updated "$module"
    done

    (while read -r; do
        for module in $modules; do
            case $REPLY in
                *" $module "*) continue 2 ;;
            esac
        done
        printf '%s\n' "$REPLY"
    done
    $GIT submodule status $modules
    test $? -ne 0 && update_error "failed to save git submodule status" >&2) < $substat > $substat.new
    mv -f $substat.new $substat
    ;;
esac

exit 0
