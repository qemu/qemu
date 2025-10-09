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
#
# The script will copy the headers into two target folders:
#
# - linux-headers/ for files that are required for compiling for a
#   Linux host.  Generally we have these so we can use kernel structs
#   and defines that are more recent than the headers that might be
#   installed on the host system.  Usually this script can do simple
#   file copies for these headers.
#
# - include/standard-headers/ for files that are used for guest
#   device emulation and are required on all hosts.  For instance, we
#   get our definitions of the virtio structures from the Linux
#   kernel headers, but we need those definitions regardless of which
#   host OS we are building for.  This script has to be careful to
#   sanitize the headers to remove any use of Linux-specifics such as
#   types like "__u64".  This work is done in the cp_portable function.

tmpdir=$(mktemp -d)
hdrdir="$tmpdir/headers"
blddir="$tmpdir/build"
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
                                     -e 'linux/ioctl' \
                                     -e 'stdint' \
                                     -e 'linux/if_ether' \
                                     -e 'input-event-codes' \
                                     -e 'sys/' \
                                     -e 'drm.h' \
                                     -e 'limits' \
                                     -e 'linux/const' \
                                     -e 'linux/kernel' \
                                     -e 'linux/sysinfo' \
                                     -e 'asm/setup_data.h' \
                                     -e 'asm/kvm_para.h' \
                                     > /dev/null
    then
        echo "Unexpected #include in input file $f".
        exit 2
    fi

    header=$(basename "$f");

    if test -z "$arch"; then
        # Let users of include/standard-headers/linux/ headers pick the
        # asm-* header that they care about
        arch_cmd='/<asm\/\([^>]*\)>/d'
    else
        arch_cmd='s/<asm\/\([^>]*\)>/"standard-headers\/asm-'$arch'\/\1"/'
    fi

    sed -e 's/__aligned_u64/__u64 __attribute__((aligned(8)))/g' \
        -e 's/__u\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/u\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/__s\([0-9][0-9]*\)/int\1_t/g' \
        -e 's/__le\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/__be\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/"\(input-event-codes\.h\)"/"standard-headers\/linux\/\1"/' \
        -e 's/<linux\/\([^>]*\)>/"standard-headers\/linux\/\1"/' \
        -e "$arch_cmd" \
        -e 's/__bitwise//' \
        -e 's/__counted_by(\w*)//' \
        -e 's/__attribute__((packed))/QEMU_PACKED/' \
        -e 's/__inline__/inline/' \
        -e 's/__BITS_PER_LONG/HOST_LONG_BITS/' \
        -e '/\"drm.h\"/d' \
        -e '/sys\/ioctl.h/d' \
        -e '/linux\/ioctl.h/d' \
        -e 's/SW_MAX/SW_MAX_/' \
        -e 's/atomic_t/int/' \
        -e 's/__kernel_long_t/long/' \
        -e 's/__kernel_ulong_t/unsigned long/' \
        -e 's/struct ethhdr/struct eth_header/' \
        -e '/\#define _LINUX_ETHTOOL_H/a \\n\#include "net/eth.h"' \
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

    if [ "$arch" = x86 ]; then
        arch_var=SRCARCH
    else
        arch_var=ARCH
    fi

    rm -rf "$hdrdir"
    make -C "$linux" O="$blddir" INSTALL_HDR_PATH="$hdrdir" $arch_var=$arch headers_install

    rm -rf "$output/linux-headers/asm-$arch"
    mkdir -p "$output/linux-headers/asm-$arch"
    for header in kvm.h unistd.h bitsperlong.h mman.h; do
        if test -f "$hdrdir/include/asm/$header"; then
            cp "$hdrdir/include/asm/$header" "$output/linux-headers/asm-$arch"
        elif test -f "$hdrdir/include/asm-generic/$header"; then
            # not installed as <asm/$header>, but used as such in kernel sources
            cat <<EOF >$output/linux-headers/asm-$arch/$header
#include <asm-generic/$header>
EOF
        fi
    done

    if [ $arch = mips ]; then
        cp "$hdrdir/include/asm/sgidefs.h" "$output/linux-headers/asm-mips/"
        cp "$hdrdir/include/asm/unistd_o32.h" "$output/linux-headers/asm-mips/"
        cp "$hdrdir/include/asm/unistd_n32.h" "$output/linux-headers/asm-mips/"
        cp "$hdrdir/include/asm/unistd_n64.h" "$output/linux-headers/asm-mips/"
    fi
    if [ $arch = powerpc ]; then
        cp "$hdrdir/include/asm/unistd_32.h" "$output/linux-headers/asm-powerpc/"
        cp "$hdrdir/include/asm/unistd_64.h" "$output/linux-headers/asm-powerpc/"
    fi

    rm -rf "$output/include/standard-headers/asm-$arch"
    mkdir -p "$output/include/standard-headers/asm-$arch"
    if [ $arch = s390 ]; then
        cp_portable "$hdrdir/include/asm/virtio-ccw.h" "$output/include/standard-headers/asm-s390/"
        cp "$hdrdir/include/asm/unistd_32.h" "$output/linux-headers/asm-s390/"
        cp "$hdrdir/include/asm/unistd_64.h" "$output/linux-headers/asm-s390/"
    fi
    if [ $arch = arm64 ]; then
        cp "$hdrdir/include/asm/sve_context.h" "$output/linux-headers/asm-arm64/"
        cp "$hdrdir/include/asm/unistd_64.h" "$output/linux-headers/asm-arm64/"
    fi
    if [ $arch = x86 ]; then
        cp "$hdrdir/include/asm/unistd_32.h" "$output/linux-headers/asm-x86/"
        cp "$hdrdir/include/asm/unistd_x32.h" "$output/linux-headers/asm-x86/"
        cp "$hdrdir/include/asm/unistd_64.h" "$output/linux-headers/asm-x86/"

        cp_portable "$hdrdir/include/asm/kvm_para.h" "$output/include/standard-headers/asm-$arch"
        cat <<EOF >$output/linux-headers/asm-$arch/kvm_para.h
#include "standard-headers/asm-$arch/kvm_para.h"
EOF

        # Remove everything except the macros from bootparam.h avoiding the
        # unnecessary import of several video/ist/etc headers
        sed -e '/__ASSEMBLER__/,/__ASSEMBLER__/d' \
               "$hdrdir/include/asm/bootparam.h" > "$hdrdir/bootparam.h"
        cp_portable "$hdrdir/bootparam.h" \
                    "$output/include/standard-headers/asm-$arch"
        cp_portable "$hdrdir/include/asm/setup_data.h" \
                    "$output/include/standard-headers/asm-x86"
    fi
    if [ $arch = riscv ]; then
        cp "$hdrdir/include/asm/ptrace.h" "$output/linux-headers/asm-riscv/"
        cp "$hdrdir/include/asm/unistd_32.h" "$output/linux-headers/asm-riscv/"
        cp "$hdrdir/include/asm/unistd_64.h" "$output/linux-headers/asm-riscv/"
    fi
    if [ $arch = loongarch ]; then
        cp "$hdrdir/include/asm/kvm_para.h" "$output/linux-headers/asm-loongarch/"
        cp "$hdrdir/include/asm/unistd_64.h" "$output/linux-headers/asm-loongarch/"
    fi
done
arch=

rm -rf "$output/linux-headers/linux"
mkdir -p "$output/linux-headers/linux"
for header in const.h stddef.h kvm.h vfio.h vfio_ccw.h vfio_zdev.h vhost.h \
              psci.h psp-sev.h userfaultfd.h memfd.h mman.h nvme_ioctl.h \
              vduse.h iommufd.h bits.h mshv.h; do
    cp "$hdrdir/include/linux/$header" "$output/linux-headers/linux"
done

rm -rf "$output/linux-headers/asm-generic"
mkdir -p "$output/linux-headers/asm-generic"
for header in unistd.h bitsperlong.h mman-common.h mman.h hugetlb_encode.h; do
    cp "$hdrdir/include/asm-generic/$header" "$output/linux-headers/asm-generic"
done

if [ -L "$linux/source" ]; then
    cp "$linux/source/COPYING" "$output/linux-headers"
else
    cp "$linux/COPYING" "$output/linux-headers"
fi

# Recent kernel sources split the copyright/license info into multiple
# files, which we need to copy. This set of licenses is the set that
# are referred to by SPDX lines in the headers we currently copy.
# We don't copy the Documentation/process/license-rules.rst which
# is also referred to by COPYING, since it's explanatory rather than license.
if [ -d "$linux/LICENSES" ]; then
    mkdir -p "$output/linux-headers/LICENSES/preferred" \
             "$output/linux-headers/LICENSES/exceptions"
    for l in preferred/GPL-2.0 preferred/BSD-2-Clause preferred/BSD-3-Clause \
             exceptions/Linux-syscall-note; do
        cp "$linux/LICENSES/$l" "$output/linux-headers/LICENSES/$l"
    done
fi

cat <<EOF >$output/linux-headers/linux/kvm_para.h
#include "standard-headers/linux/kvm_para.h"
#include <asm/kvm_para.h>
EOF
cat <<EOF >$output/linux-headers/linux/virtio_config.h
#include "standard-headers/linux/virtio_config.h"
EOF
cat <<EOF >$output/linux-headers/linux/virtio_ring.h
#include "standard-headers/linux/virtio_ring.h"
EOF
cat <<EOF >$output/linux-headers/linux/vhost_types.h
#include "standard-headers/linux/vhost_types.h"
EOF

rm -rf "$output/include/standard-headers/linux"
mkdir -p "$output/include/standard-headers/linux"
for i in "$hdrdir"/include/linux/*virtio*.h \
         "$hdrdir/include/linux/qemu_fw_cfg.h" \
         "$hdrdir/include/linux/fuse.h" \
         "$hdrdir/include/linux/input.h" \
         "$hdrdir/include/linux/input-event-codes.h" \
         "$hdrdir/include/linux/udmabuf.h" \
         "$hdrdir/include/linux/pci_regs.h" \
         "$hdrdir/include/linux/ethtool.h" \
         "$hdrdir/include/linux/const.h" \
         "$hdrdir/include/linux/kernel.h" \
         "$hdrdir/include/linux/kvm_para.h" \
         "$hdrdir/include/linux/vhost_types.h" \
         "$hdrdir/include/linux/vmclock-abi.h" \
         "$hdrdir/include/linux/sysinfo.h"; do
    cp_portable "$i" "$output/include/standard-headers/linux"
done
mkdir -p "$output/include/standard-headers/misc"
cp_portable "$hdrdir/include/misc/pvpanic.h" \
            "$output/include/standard-headers/misc"
mkdir -p "$output/include/standard-headers/drm"
cp_portable "$hdrdir/include/drm/drm_fourcc.h" \
            "$output/include/standard-headers/drm"

cat <<EOF >$output/include/standard-headers/linux/types.h
/* For QEMU all types are already defined via osdep.h, so this
 * header does not need to do anything.
 */
EOF
cat <<EOF >$output/include/standard-headers/linux/if_ether.h
#define ETH_ALEN    6
EOF

rm -rf "$tmpdir"
