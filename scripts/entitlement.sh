#!/bin/sh -e
#
# Helper script for the build process to apply entitlements

SRC="$1"
DST="$2"
ENTITLEMENT="$3"

trap 'rm "$DST.tmp"' exit
cp -af "$SRC" "$DST.tmp"
codesign --entitlements "$ENTITLEMENT" --force -s - "$DST.tmp"
mv "$DST.tmp" "$DST"
trap '' exit
