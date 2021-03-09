#!/bin/bash
#
# Test parallels load bitmap
#
# Copyright (c) 2021 Virtuozzo International GmbH.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

CT=parallels-with-bitmap-ct
DIR=$PWD/parallels-with-bitmap-dir
IMG=$DIR/root.hds
XML=$DIR/DiskDescriptor.xml
TARGET=parallels-with-bitmap.bz2

rm -rf $DIR

prlctl create $CT --vmtype ct
prlctl set $CT --device-add hdd --image $DIR --recreate --size 2G

# cleanup the image
qemu-img create -f parallels $IMG 64G

# create bitmap
prlctl backup $CT

prlctl set $CT --device-del hdd1
prlctl destroy $CT

dev=$(ploop mount $XML | sed -n 's/^Adding delta dev=\(\/dev\/ploop[0-9]\+\).*/\1/p')
dd if=/dev/zero of=$dev bs=64K seek=5 count=2 oflag=direct
dd if=/dev/zero of=$dev bs=64K seek=30 count=1 oflag=direct
dd if=/dev/zero of=$dev bs=64K seek=10 count=3 oflag=direct
ploop umount $XML  # bitmap name will be in the output

bzip2 -z $IMG

mv $IMG.bz2 $TARGET

rm -rf $DIR
