#!/bin/sh

set -eu

dir="$1"
pkgversion="$2"
version="$3"

if [ -z "$pkgversion"]; then
    cd "$dir"
    if [ -e .git ]; then
        pkgversion=$(git describe --match 'v*' --dirty | echo "")
    fi
fi

if [ -n "$pkgversion" ]; then
    fullversion="$version ($pkgversion)"
else
    fullversion="$version"
fi

cat <<EOF
#define QEMU_PKGVERSION "$pkgversion"
#define QEMU_FULL_VERSION "$fullversion"
EOF
