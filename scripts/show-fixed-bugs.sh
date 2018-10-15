#!/bin/sh

# This script checks the git log for URLs to the QEMU launchpad bugtracker
# and optionally checks whether the corresponding bugs are not closed yet.

show_help () {
    echo "Usage:"
    echo "  -s <commit>  : Start searching at this commit"
    echo "  -e <commit>  : End searching at this commit"
    echo "  -c           : Check if bugs are still open"
    echo "  -b           : Open bugs in browser"
}

while getopts "s:e:cbh" opt; do
   case "$opt" in
    s)  start="$OPTARG" ;;
    e)  end="$OPTARG" ;;
    c)  check_if_open=1 ;;
    b)  show_in_browser=1 ;;
    h)  show_help ; exit 0 ;;
    *)   echo "Use -h for help." ; exit 1 ;;
   esac
done

if [ "x$start" = "x" ]; then
    start=$(git tag -l 'v[0-9]*\.[0-9]*\.0' | tail -n 2 | head -n 1)
fi
if [ "x$end" = "x" ]; then
    end=$(git tag -l  'v[0-9]*\.[0-9]*\.0' | tail -n 1)
fi

if [ "x$start" = "x" ] || [ "x$end" = "x" ]; then
    echo "Could not determine start or end revision ... Please note that this"
    echo "script must be run from a checked out git repository of QEMU."
    exit 1
fi

echo "Searching git log for bugs in the range $start..$end"

urlstr='https://bugs.launchpad.net/\(bugs\|qemu/+bug\)/'
bug_urls=$(git log $start..$end \
  | sed -n '\,'"$urlstr"', s,\(.*\)\('"$urlstr"'\)\([0-9]*\).*,\2\4,p' \
  | sort -u)

echo Found bug URLs:
for i in $bug_urls ; do echo " $i" ; done

if [ "x$check_if_open" = "x1" ]; then
    echo
    echo "Checking which ones are still open..."
    for i in $bug_urls ; do
        if ! curl -s -L "$i" | grep "value status" | grep -q "Fix Released" ; then
            echo " $i"
            final_bug_urls="$final_bug_urls $i"
        fi
    done
else
    final_bug_urls=$bug_urls
fi

if [ "x$final_bug_urls" = "x" ]; then
    echo "No open bugs found."
elif [ "x$show_in_browser" = "x1" ]; then
    # Try to determine which browser we should use
    if [ "x$BROWSER" != "x" ]; then
        bugbrowser="$BROWSER"
    elif command -v xdg-open >/dev/null 2>&1; then
        bugbrowser=xdg-open
    elif command -v gnome-open >/dev/null 2>&1; then
        bugbrowser=gnome-open
    elif [ "$(uname)" = "Darwin" ]; then
        bugbrowser=open
    elif command -v sensible-browser >/dev/null 2>&1; then
        bugbrowser=sensible-browser
    else
        echo "Please set the BROWSER variable to the browser of your choice."
        exit 1
    fi
    # Now show the bugs in the browser
    first=1
    for i in $final_bug_urls; do
        "$bugbrowser" "$i"
        if [ $first = 1 ]; then
            # if it is the first entry, give the browser some time to start
            # (to avoid messages like "Firefox is already running, but is
            # not responding...")
            sleep 4
            first=0
        fi
    done
fi
