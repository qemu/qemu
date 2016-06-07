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

tmpdir=$(mktemp -d)
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

cp_portable() {
    f=$1
    to=$2
    if
        grep '#include' "$f" | grep -v -e 'linux/virtio' \
                                     -e 'linux/types' \
                                     -e 'stdint' \
                                     -e 'linux/if_ether' \
                                     -e 'input-event-codes' \
                                     -e 'sys/' \
                                     > /dev/null
    then
        echo "Unexpected #include in input file $f".
        exit 2
    fi

    header=$(basename "$f");
    sed -e 's/__u\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/__s\([0-9][0-9]*\)/int\1_t/g' \
        -e 's/__le\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/__be\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/"\(input-event-codes\.h\)"/"standard-headers\/linux\/\1"/' \
        -e 's/<linux\/\([^>]*\)>/"standard-headers\/linux\/\1"/' \
        -e 's/__bitwise__//' \
        -e 's/__attribute__((packed))/QEMU_PACKED/' \
        -e 's/__inline__/inline/' \
        -e '/sys\/ioctl.h/d' \
        -e 's/SW_MAX/SW_MAX_/' \
        "$f" > "$to/$header";
}

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
    if [ "$arch" = "ia64" -o "$arch" = "mips" ]; then
        continue
    fi

    make -C "$linux" INSTALL_HDR_PATH="$tmpdir" SRCARCH=$arch headers_install

    rm -rf "$output/linux-headers/asm-$arch"
    mkdir -p "$output/linux-headers/asm-$arch"
    for header in kvm.h kvm_para.h unistd.h; do
        cp "$tmpdir/include/asm/$header" "$output/linux-headers/asm-$arch"
    done
    if [ $arch = powerpc ]; then
        cp "$tmpdir/include/asm/epapr_hcalls.h" "$output/linux-headers/asm-powerpc/"
    fi

    rm -rf "$output/include/standard-headers/asm-$arch"
    mkdir -p "$output/include/standard-headers/asm-$arch"
    if [ $arch = s390 ]; then
        cp_portable "$tmpdir/include/asm/kvm_virtio.h" "$output/include/standard-headers/asm-s390/"
        cp_portable "$tmpdir/include/asm/virtio-ccw.h" "$output/include/standard-headers/asm-s390/"
    fi
    if [ $arch = x86 ]; then
        cp_portable "$tmpdir/include/asm/hyperv.h" "$output/include/standard-headers/asm-x86/"
        cp "$tmpdir/include/asm/unistd_32.h" "$output/linux-headers/asm-x86/"
        cp "$tmpdir/include/asm/unistd_x32.h" "$output/linux-headers/asm-x86/"
        cp "$tmpdir/include/asm/unistd_64.h" "$output/linux-headers/asm-x86/"
    fi
done

rm -rf "$output/linux-headers/linux"
mkdir -p "$output/linux-headers/linux"
for header in kvm.h kvm_para.h vfio.h vhost.h \
              psci.h userfaultfd.h; do
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

cat <<EOF >$output/linux-headers/asm-x86/hyperv.h
#include "standard-headers/asm-x86/hyperv.h"
EOF
cat <<EOF >$output/linux-headers/linux/virtio_config.h
#include "standard-headers/linux/virtio_config.h"
EOF
cat <<EOF >$output/linux-headers/linux/virtio_ring.h
#include "standard-headers/linux/virtio_ring.h"
EOF

rm -rf "$output/include/standard-headers/linux"
mkdir -p "$output/include/standard-headers/linux"
for i in "$tmpdir"/include/linux/*virtio*.h "$tmpdir/include/linux/input.h" \
         "$tmpdir/include/linux/input-event-codes.h" \
         "$tmpdir/include/linux/pci_regs.h"; do
    cp_portable "$i" "$output/include/standard-headers/linux"
done

cat <<EOF >$output/include/standard-headers/linux/types.h
/* For QEMU all types are already defined via osdep.h, so this
 * header does not need to do anything.
 */
EOF
cat <<EOF >$output/include/standard-headers/linux/if_ether.h
#define ETH_ALEN    6
EOF

rm -rf "$tmpdir"
