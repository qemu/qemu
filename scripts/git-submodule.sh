#!/bin/sh
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

substat=".git-submodule-status"

command=$1
shift
maybe_modules="$@"

test -z "$GIT" && GIT=git

error() {
    echo "$0: $*"
    echo
    echo "Unable to automatically checkout GIT submodules '$modules'."
    echo "If you require use of an alternative GIT binary (for example to"
    echo "enable use of a transparent proxy), then please specify it by"
    echo "running configure by with the '--with-git' argument. e.g."
    echo
    echo " $ ./configure --with-git='tsocks git'"
    echo
    echo "Alternatively you may disable automatic GIT submodule checkout"
    echo "with:"
    echo
    echo " $ ./configure --disable-git-update"
    echo
    echo "and then manually update submodules prior to running make, with:"
    echo
    echo " $ scripts/git-submodule.sh update $modules"
    echo
    exit 1
}

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

if test -n "$maybe_modules" && ! test -e ".git"
then
    echo "$0: unexpectedly called with submodules but no git checkout exists"
    exit 1
fi

case "$command" in
status)
    if test -z "$maybe_modules"
    then
         test -s ${substat} && exit 1 || exit 0
    fi

    test -f "$substat" || exit 1
    for module in $modules; do
        CURSTATUS=$($GIT submodule status $module)
        OLDSTATUS=$(cat $substat | grep $module)
        if test "$CURSTATUS" != "$OLDSTATUS"; then
            exit 1
        fi
    done
    exit 0
    ;;
update)
    if test -z "$maybe_modules"
    then
        test -e $substat || touch $substat
        exit 0
    fi

    $GIT submodule update --init $modules 1>/dev/null
    test $? -ne 0 && error "failed to update modules"

    $GIT submodule status $modules > "${substat}"
    test $? -ne 0 && error "failed to save git submodule status" >&2
    ;;
esac

exit 0
