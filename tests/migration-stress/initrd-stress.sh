#!/bin/sh

INITRD="$1"
STRESS="$2"

INITRD_DIR=$(mktemp -d -p '' "initrd-stress.XXXXXX")
trap 'rm -rf $INITRD_DIR' EXIT

cp "$STRESS" "$INITRD_DIR/init"
(cd "$INITRD_DIR" && (find | cpio --quiet -o -H newc | gzip -9)) > "$INITRD"
