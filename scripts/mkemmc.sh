#!/bin/sh -e
# SPDX-License-Identifier: GPL-2.0-only
#
# Create eMMC block device image from boot, RPMB and user data images
#
# Copyright (c) Siemens, 2025
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#

usage() {
    echo "$0 [OPTIONS] USER_IMG[:SIZE] OUTPUT_IMG"
    echo ""
    echo "SIZE must be a power of 2 up to 2G and multiples of 512 byte from there on."
    echo "If no SIZE is specified, the size of USER_ING will be used (rounded up)."
    echo ""
    echo "Supported options:"
    echo "  -b BOOT1_IMG[:SIZE]   Add boot partitions. SIZE must be multiples of 128K. If"
    echo "                          no SIZE is specified, the size of BOOT1_IMG will be"
    echo "                          used (rounded up). BOOT1_IMG will be stored in boot"
    echo "                          partition 1, and a boot partition 2 of the same size"
    echo "                          will be created as empty (all zeros) unless -B is"
    echo "                          specified as well."
    echo "  -B BOOT2_IMG          Fill boot partition 2 with BOOT2_IMG. Must be combined"
    echo "                          with -b which is also defining the partition size."
    echo "  -r RPMB_IMG[:SIZE]    Add RPMB partition. SIZE must be multiples of 128K. If"
    echo "                          no SIZE is specified, the size of RPMB_IMG will be"
    echo "                          used (rounded up)."
    echo "  -h, --help            This help"
    echo ""
    echo "All SIZE parameters support the units K, M, G. If SIZE is smaller than the"
    echo "associated image, it will be truncated in the output image."
    exit "$1"
}

process_size() {
    name=$1
    image_file=$2
    alignment=$3
    image_arg=$4
    if [ "${image_arg#*:}" = "$image_arg"  ]; then
        if ! size=$(wc -c < "$image_file" 2>/dev/null); then
            echo "Missing $name image '$image_file'." >&2
            exit 1
        fi
        if [ "$alignment" = 128 ]; then
            size=$(( (size + 128 * 1024 - 1) & ~(128 * 1024 - 1) ))
        elif [ $size -gt $((2 * 1024 * 1024 * 1024)) ]; then
            size=$(( (size + 511) & ~511 ))
        elif [ $(( size & (size - 1) )) -gt 0 ]; then
            n=0
            while [ "$size" -gt 0 ]; do
                size=$((size >> 1))
                n=$((n + 1))
            done
            size=$((1 << n))
        fi
    else
        value="${image_arg#*:}"
        if [ "${value%K}" != "$value" ]; then
            size=${value%K}
            multiplier=1024
        elif [ "${value%M}" != "$value" ]; then
            size=${value%M}
            multiplier=$((1024 * 1024))
        elif [ "${value%G}" != "$value" ]; then
            size=${value%G}
            multiplier=$((1024 * 1024 * 1024))
        else
            size=$value
            multiplier=1
        fi
        # check if "$size" is a valid integer by doing a self-comparison
        if [ "$size" -eq "$size" ] 2>/dev/null; then
            size=$((size * multiplier))
        else
            echo "Invalid value '$value' specified for $image_file image size." >&2
            exit 1
        fi
        if [ "$alignment" = 128 ]; then
            if [ $(( size & (128 * 1024 - 1) )) -ne 0 ]; then
                echo "The $name image size must be multiples of 128K." >&2
                exit 1
            fi
        elif [ $size -gt $((2 * 1024 * 1024 * 1024)) ]; then
            if [ $(( size & 511)) -ne 0 ]; then
                echo "The $name image size must be multiples of 512 (if >2G)." >&2
                exit 1
            fi
        elif [ $(( size & (size - 1) )) -gt 0 ]; then
            echo "The $name image size must be power of 2 (up to 2G)." >&2
            exit 1
        fi
    fi
    echo $size
}

check_truncation() {
    image_file=$1
    output_size=$2
    if [ "$image_file" = "/dev/zero" ]; then
        return
    fi
    if ! actual_size=$(wc -c < "$image_file" 2>/dev/null); then
        echo "Missing image '$image_file'." >&2
        exit 1
    fi
    if [ "$actual_size" -gt "$output_size" ]; then
        echo "Warning: image '$image_file' will be truncated on output."
    fi
}

userimg=
outimg=
bootimg1=
bootimg2=/dev/zero
bootsz=0
rpmbimg=
rpmbsz=0

while [ $# -gt 0 ]; do
    case "$1" in
        -b)
            shift
            [ $# -ge 1 ] || usage 1
            bootimg1=${1%%:*}
            bootsz=$(process_size boot "$bootimg1" 128 "$1")
            shift
            ;;
        -B)
            shift
            [ $# -ge 1 ] || usage 1
            bootimg2=$1
            shift
            ;;
        -r)
            shift
            [ $# -ge 1 ] || usage 1
            rpmbimg=${1%%:*}
            rpmbsz=$(process_size RPMB "$rpmbimg" 128 "$1")
            shift
            ;;
        -h|--help)
            usage 0
            ;;
        *)
            if [ -z "$userimg" ]; then
                userimg=${1%%:*}
                usersz=$(process_size user "$userimg" U "$1")
            elif [ -z "$outimg" ]; then
                outimg=$1
            else
                usage 1
            fi
            shift
            ;;
    esac
done

[ -n "$outimg" ] || usage 1

if [ "$bootsz" -gt $((32640 * 1024)) ]; then
    echo "Boot image size is larger than 32640K." >&2
    exit 1
fi
if [ "$rpmbsz" -gt $((16384 * 1024)) ]; then
    echo "RPMB image size is larger than 16384K." >&2
    exit 1
fi

echo "Creating eMMC image"

truncate -s 0 "$outimg"
pos=0

if [ "$bootsz" -gt 0 ]; then
    echo "  Boot partition 1 and 2:   $((bootsz / 1024))K each"
    blocks=$(( bootsz / (128 * 1024) ))
    check_truncation "$bootimg1" "$bootsz"
    dd if="$bootimg1" of="$outimg" conv=sparse bs=128K count=$blocks \
        status=none
    check_truncation "$bootimg2" "$bootsz"
    dd if="$bootimg2" of="$outimg" conv=sparse bs=128K count=$blocks \
        seek=$blocks status=none
    pos=$((2 * bootsz))
fi

if [ "$rpmbsz" -gt 0 ]; then
    echo "  RPMB partition:           $((rpmbsz / 1024))K"
    blocks=$(( rpmbsz / (128 * 1024) ))
    check_truncation "$rpmbimg" "$rpmbsz"
    dd if="$rpmbimg" of="$outimg" conv=sparse bs=128K count=$blocks \
        seek=$(( pos / (128 * 1024) )) status=none
    pos=$((pos + rpmbsz))
fi

if [ "$usersz" -lt 1024 ]; then
    echo "  User data:                $usersz bytes"
elif [ "$usersz" -lt $((1024 * 1024)) ]; then
    echo "  User data:                $(( (usersz + 1023) / 1024 ))K ($usersz)"
elif [ "$usersz" -lt $((1024 * 1024 * 1024)) ]; then
    echo "  User data:                $(( (usersz + 1048575) / 1048576))M ($usersz)"
else
    echo "  User data:                $(( (usersz + 1073741823) / 1073741824))G ($usersz)"
fi
check_truncation "$userimg" "$usersz"
dd if="$userimg" of="$outimg" conv=sparse bs=128K seek=$(( pos / (128 * 1024) )) \
    count=$(( (usersz + 128 * 1024 - 1) / (128 * 1024) )) status=none
pos=$((pos + usersz))
truncate -s $pos "$outimg"

echo ""
echo "Instantiate by appending to the qemu command line:"
echo "  -drive file=$outimg,if=none,format=raw,id=emmc-img"
echo "  -device emmc,boot-partition-size=$bootsz,rpmb-partition-size=$rpmbsz,drive=emmc-img"
