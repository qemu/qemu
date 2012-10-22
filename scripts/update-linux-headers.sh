#!/bin/sh -e
#
# Update Linux kernel headers QEMU requires from a specified kernel tree.
#
# Copyright (C) 2011 Siemens AG
#
# Authors:
#  Jan Kiszka        <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
# See the COPYING file in the top-level directory.

tmpdir=`mktemp -d`
linux="$1"
output="$2"

if [ -z "$linux" ] || ! [ -d "$linux" ]; then
    cat << EOF
usage: update-kernel-headers.sh LINUX_PATH [OUTPUT_PATH]

LINUX_PATH      Linux kernel directory to obtain the headers from
OUTPUT_PATH     output directory, usually the qemu source tree (default: $PWD)
EOF
    exit 1
fi

if [ -z "$output" ]; then
    output="$PWD"
fi

# This will pick up non-directories too (eg "Kconfig") but we will
# ignore them in the next loop.
ARCHLIST=$(cd "$linux/arch" && echo *)

for arch in $ARCHLIST; do
    # Discard anything which isn't a KVM-supporting architecture
    if ! [ -e "$linux/arch/$arch/include/asm/kvm.h" ] &&
        ! [ -e "$linux/arch/$arch/include/uapi/asm/kvm.h" ] ; then
        continue
    fi

    # Blacklist architectures which have KVM headers but are actually dead
    if [ "$arch" = "ia64" ]; then
        continue
    fi

    make -C "$linux" INSTALL_HDR_PATH="$tmpdir" SRCARCH=$arch headers_install

    rm -rf "$output/linux-headers/asm-$arch"
    mkdir -p "$output/linux-headers/asm-$arch"
    for header in kvm.h kvm_para.h; do
        cp "$tmpdir/include/asm/$header" "$output/linux-headers/asm-$arch"
    done
    if [ $arch = x86 ]; then
        cp "$tmpdir/include/asm/hyperv.h" "$output/linux-headers/asm-x86"
    fi
done

rm -rf "$output/linux-headers/linux"
mkdir -p "$output/linux-headers/linux"
for header in kvm.h kvm_para.h vfio.h vhost.h virtio_config.h virtio_ring.h; do
    cp "$tmpdir/include/linux/$header" "$output/linux-headers/linux"
done
rm -rf "$output/linux-headers/asm-generic"
mkdir -p "$output/linux-headers/asm-generic"
for header in kvm_para.h; do
    cp "$tmpdir/include/asm-generic/$header" "$output/linux-headers/asm-generic"
done
if [ -L "$linux/source" ]; then
    cp "$linux/source/COPYING" "$output/linux-headers"
else
    cp "$linux/COPYING" "$output/linux-headers"
fi

rm -rf "$tmpdir"
