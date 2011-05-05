#!/bin/bash

# PXE ROM build script
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#
# Copyright (C) 2011 Red Hat, Inc.
#   Authors: Alex Williamson <alex.williamson@redhat.com>
#
# Usage: Run from root of qemu tree
# ./scripts/refresh-pxe-roms.sh

QEMU_DIR=$PWD
ROM_DIR="pc-bios"
BUILD_DIR="roms/ipxe"
LOCAL_CONFIG="src/config/local/general.h"

function cleanup ()
{
    if [ -n "$SAVED_CONFIG" ]; then
        cp "$SAVED_CONFIG" "$BUILD_DIR"/"$LOCAL_CONFIG"
        rm "$SAVED_CONFIG"
    fi
    cd "$QEMU_DIR"
}

function make_rom ()
{
    cd "$BUILD_DIR"/src

    BUILD_LOG=$(mktemp)

    echo Building "$2"...
    make bin/"$1".rom > "$BUILD_LOG" 2>&1
    if [ $? -ne 0 ]; then
        echo Build failed
        tail --lines=100 "$BUILD_LOG"
        rm "$BUILD_LOG"
        cleanup
        exit 1
    fi
    rm "$BUILD_LOG"

    cp bin/"$1".rom "$QEMU_DIR"/"$ROM_DIR"/"$2"

    cd "$QEMU_DIR"
}

if [ ! -d "$QEMU_DIR"/"$ROM_DIR" ]; then
    echo "error: can't find $ROM_DIR directory," \
         "run me from the root of the qemu tree"
    exit 1
fi

if [ ! -d "$BUILD_DIR"/src ]; then
    echo "error: $BUILD_DIR not populated, try:"
    echo "  git submodule init $BUILD_DIR"
    echo "  git submodule update $BUILD_DIR"
    exit 1
fi

if [ -e "$BUILD_DIR"/"$LOCAL_CONFIG" ]; then
    SAVED_CONFIG=$(mktemp)
    cp "$BUILD_DIR"/"$LOCAL_CONFIG" "$SAVED_CONFIG"
fi

echo "#undef BANNER_TIMEOUT" > "$BUILD_DIR"/"$LOCAL_CONFIG"
echo "#define BANNER_TIMEOUT 0" >> "$BUILD_DIR"/"$LOCAL_CONFIG"

IPXE_VERSION=$(cd "$BUILD_DIR" && git describe --tags)
if [ -z "$IPXE_VERSION" ]; then
    echo "error: unable to retrieve git version"
    cleanup
    exit 1
fi

echo "#undef PRODUCT_NAME" >> "$BUILD_DIR"/"$LOCAL_CONFIG"
echo "#define PRODUCT_NAME \"iPXE $IPXE_VERSION\"" >> "$BUILD_DIR"/"$LOCAL_CONFIG"

make_rom 8086100e pxe-e1000.rom
make_rom 80861209 pxe-eepro100.rom
make_rom 10500940 pxe-ne2k_pci.rom
make_rom 10222000 pxe-pcnet.rom
make_rom 10ec8139 pxe-rtl8139.rom
make_rom 1af41000 pxe-virtio.rom

echo done
cleanup
