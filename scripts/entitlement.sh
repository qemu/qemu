#!/bin/sh -e
#
# Helper script for the build process to apply entitlements

in_place=:
if [ "$1" = --install ]; then
  shift
  in_place=false
fi

SRC="$1"
DST="$2"
ENTITLEMENT="$3"

if $in_place; then
  trap 'rm "$DST.tmp"' exit
  cp -af "$SRC" "$DST.tmp"
  SRC="$DST.tmp"
else
  cd "$MESON_INSTALL_DESTDIR_PREFIX"
fi

codesign --entitlements "$ENTITLEMENT" --force -s - "$SRC"
mv -f "$SRC" "$DST"
trap '' exit
