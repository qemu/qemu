#!/bin/sh -e
#
# Helper script for the build process to apply entitlements

in_place=:
if [ "$1" = --install ]; then
  shift
  in_place=false
fi

DST="$1"
SRC="$2"
ICON="$3"
ENTITLEMENT="$4"

if $in_place; then
  trap 'rm "$DST.tmp"' exit
  cp -af "$SRC" "$DST.tmp"
  SRC="$DST.tmp"
else
  cd "$MESON_INSTALL_DESTDIR_PREFIX"
fi

if test -n "$ENTITLEMENT"; then
  codesign --entitlements "$ENTITLEMENT" --force -s - "$SRC"
fi

# Add the QEMU icon to the binary on Mac OS
Rez -append "$ICON" -o "$SRC"
SetFile -a C "$SRC"

mv -f "$SRC" "$DST"
trap '' exit
