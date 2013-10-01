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

targets="pxerom"
if test -x "$(which EfiRom 2>/dev/null)"; then
    targets="$targets efirom"
fi

cd roms
make -j4 $targets || exit 1
make clean
