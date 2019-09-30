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
                                     -e 'pvrdma_verbs' \
                                     -e 'drm.h' \
                                     -e 'limits' \
                                     -e 'linux/kernel' \
                                     -e 'linux/sysinfo' \
                                     -e 'asm-generic/kvm_para' \
                                     > /dev/null
    then
        echo "Unexpected #include in input file $f".
        exit 2
    fi

    header=$(basename "$f");
    sed -e 's/__aligned_u64/__u64 __attribute__((aligned(8)))/g' \
        -e 's/__u\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/u\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/__s\([0-9][0-9]*\)/int\1_t/g' \
        -e 's/__le\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/__be\([0-9][0-9]*\)/uint\1_t/g' \
        -e 's/"\(input-event-codes\.h\)"/"standard-headers\/linux\/\1"/' \
        -e 's/<linux\/\([^>]*\)>/"standard-headers\/linux\/\1"/' \
        -e 's/__bitwise//' \
        -e 's/__attribute__((packed))/QEMU_PACKED/' \
        -e 's/__inline__/inline/' \
        -e 's/__BITS_PER_LONG/HOST_LONG_BITS/' \
        -e '/\"drm.h\"/d' \
        -e '/sys\/ioctl.h/d' \
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

    make -C "$linux" INSTALL_HDR_PATH="$tmpdir" $arch_var=$arch headers_install

    rm -rf "$output/linux-headers/asm-$arch"
    mkdir -p "$output/linux-headers/asm-$arch"
    for header in kvm.h unistd.h bitsperlong.h mman.h; do
        cp "$tmpdir/include/asm/$header" "$output/linux-headers/asm-$arch"
    done

    if [ $arch = mips ]; then
        cp "$tmpdir/include/asm/sgidefs.h" "$output/linux-headers/asm-mips/"
        cp "$tmpdir/include/asm/unistd_o32.h" "$output/linux-headers/asm-mips/"
        cp "$tmpdir/include/asm/unistd_n32.h" "$output/linux-headers/asm-mips/"
        cp "$tmpdir/include/asm/unistd_n64.h" "$output/linux-headers/asm-mips/"
    fi
    if [ $arch = powerpc ]; then
        cp "$tmpdir/include/asm/unistd_32.h" "$output/linux-headers/asm-powerpc/"
        cp "$tmpdir/include/asm/unistd_64.h" "$output/linux-headers/asm-powerpc/"
    fi

    rm -rf "$output/include/standard-headers/asm-$arch"
    mkdir -p "$output/include/standard-headers/asm-$arch"
    if [ $arch = s390 ]; then
        cp_portable "$tmpdir/include/asm/virtio-ccw.h" "$output/include/standard-headers/asm-s390/"
        cp "$tmpdir/include/asm/unistd_32.h" "$output/linux-headers/asm-s390/"
        cp "$tmpdir/include/asm/unistd_64.h" "$output/linux-headers/asm-s390/"
    fi
    if [ $arch = arm ]; then
        cp "$tmpdir/include/asm/unistd-eabi.h" "$output/linux-headers/asm-arm/"
        cp "$tmpdir/include/asm/unistd-oabi.h" "$output/linux-headers/asm-arm/"
        cp "$tmpdir/include/asm/unistd-common.h" "$output/linux-headers/asm-arm/"
    fi
    if [ $arch = arm64 ]; then
        cp "$tmpdir/include/asm/sve_context.h" "$output/linux-headers/asm-arm64/"
    fi
    if [ $arch = x86 ]; then
        cp "$tmpdir/include/asm/unistd_32.h" "$output/linux-headers/asm-x86/"
        cp "$tmpdir/include/asm/unistd_x32.h" "$output/linux-headers/asm-x86/"
        cp "$tmpdir/include/asm/unistd_64.h" "$output/linux-headers/asm-x86/"
        cp_portable "$tmpdir/include/asm/kvm_para.h" "$output/include/standard-headers/asm-$arch"
        # Remove everything except the macros from bootparam.h avoiding the
        # unnecessary import of several video/ist/etc headers
        sed -e '/__ASSEMBLY__/,/__ASSEMBLY__/d' \
               "$tmpdir/include/asm/bootparam.h" > "$tmpdir/bootparam.h"
        cp_portable "$tmpdir/bootparam.h" \
                    "$output/include/standard-headers/asm-$arch"
    fi
done

rm -rf "$output/linux-headers/linux"
mkdir -p "$output/linux-headers/linux"
for header in kvm.h vfio.h vfio_ccw.h vhost.h \
              psci.h psp-sev.h userfaultfd.h mman.h; do
    cp "$tmpdir/include/linux/$header" "$output/linux-headers/linux"
done

rm -rf "$output/linux-headers/asm-generic"
mkdir -p "$output/linux-headers/asm-generic"
for header in unistd.h bitsperlong.h mman-common.h mman.h hugetlb_encode.h; do
    cp "$tmpdir/include/asm-generic/$header" "$output/linux-headers/asm-generic"
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
for i in "$tmpdir"/include/linux/*virtio*.h \
         "$tmpdir/include/linux/qemu_fw_cfg.h" \
         "$tmpdir/include/linux/fuse.h" \
         "$tmpdir/include/linux/input.h" \
         "$tmpdir/include/linux/input-event-codes.h" \
         "$tmpdir/include/linux/pci_regs.h" \
         "$tmpdir/include/linux/ethtool.h" "$tmpdir/include/linux/kernel.h" \
         "$tmpdir/include/linux/vhost_types.h" \
         "$tmpdir/include/linux/sysinfo.h"; do
    cp_portable "$i" "$output/include/standard-headers/linux"
done
mkdir -p "$output/include/standard-headers/drm"
cp_portable "$tmpdir/include/drm/drm_fourcc.h" \
            "$output/include/standard-headers/drm"

rm -rf "$output/include/standard-headers/drivers/infiniband/hw/vmw_pvrdma"
mkdir -p "$output/include/standard-headers/drivers/infiniband/hw/vmw_pvrdma"

# Remove the unused functions from pvrdma_verbs.h avoiding the unnecessary
# import of several infiniband/networking/other headers
tmp_pvrdma_verbs="$tmpdir/pvrdma_verbs.h"
# Parse the entire file instead of single lines to match
# function declarations expanding over multiple lines
# and strip the declarations starting with pvrdma prefix.
sed  -e '1h;2,$H;$!d;g'  -e 's/[^};]*pvrdma[^(| ]*([^)]*);//g' \
    "$linux/drivers/infiniband/hw/vmw_pvrdma/pvrdma_verbs.h" > \
    "$tmp_pvrdma_verbs";

for i in "$linux/drivers/infiniband/hw/vmw_pvrdma/pvrdma_ring.h" \
         "$linux/drivers/infiniband/hw/vmw_pvrdma/pvrdma_dev_api.h" \
         "$tmp_pvrdma_verbs"; do \
    cp_portable "$i" \
         "$output/include/standard-headers/drivers/infiniband/hw/vmw_pvrdma/"
done

rm -rf "$output/include/standard-headers/rdma/"
mkdir -p "$output/include/standard-headers/rdma/"
for i in "$tmpdir/include/rdma/vmw_pvrdma-abi.h"; do
    cp_portable "$i" \
         "$output/include/standard-headers/rdma/"
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
