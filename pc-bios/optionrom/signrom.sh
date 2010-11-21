#!/bin/sh

# Option ROM Signing utility
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
# Copyright Novell Inc, 2009
#   Authors: Alexander Graf <agraf@suse.de>
#
# Syntax: signrom.sh <input> <output>

# did we get proper arguments?
test "$1" -a "$2" || exit 1

sum=0

# find out the file size
x=`dd if="$1" bs=1 count=1 skip=2 2>/dev/null | od -t u1 -A n`
#size=`expr $x \* 512 - 1`
size=$(( $x * 512 - 1 ))

# now get the checksum
nums=`od -A n -t u1 -v -N $size "$1"`
for i in ${nums}; do
    # add each byte's value to sum
    sum=`expr \( $sum + $i \) % 256`
done

sum=$(( (256 - $sum) % 256 ))
sum_octal=$( printf "%o" $sum )

# and write the output file
cp "$1" "$2"
printf "\\$sum_octal" | dd of="$2" bs=1 count=1 seek=$size conv=notrunc 2>/dev/null
