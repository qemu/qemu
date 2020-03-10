TBL_LIST="\
arch/alpha/kernel/syscalls/syscall.tbl,linux-user/alpha/syscall.tbl \
arch/arm/tools/syscall.tbl,linux-user/arm/syscall.tbl \
arch/m68k/kernel/syscalls/syscall.tbl,linux-user/m68k/syscall.tbl \
arch/microblaze/kernel/syscalls/syscall.tbl,linux-user/microblaze/syscall.tbl \
arch/mips/kernel/syscalls/syscall_n32.tbl,linux-user/mips64/syscall_n32.tbl \
arch/mips/kernel/syscalls/syscall_n64.tbl,linux-user/mips64/syscall_n64.tbl \
arch/mips/kernel/syscalls/syscall_o32.tbl,linux-user/mips/syscall_o32.tbl \
arch/parisc/kernel/syscalls/syscall.tbl,linux-user/hppa/syscall.tbl \
arch/powerpc/kernel/syscalls/syscall.tbl,linux-user/ppc/syscall.tbl \
arch/s390/kernel/syscalls/syscall.tbl,linux-user/s390x/syscall.tbl \
arch/sh/kernel/syscalls/syscall.tbl,linux-user/sh4/syscall.tbl \
arch/sparc/kernel/syscalls/syscall.tbl,linux-user/sparc64/syscall.tbl \
arch/sparc/kernel/syscalls/syscall.tbl,linux-user/sparc/syscall.tbl \
arch/x86/entry/syscalls/syscall_32.tbl,linux-user/i386/syscall_32.tbl \
arch/x86/entry/syscalls/syscall_64.tbl,linux-user/x86_64/syscall_64.tbl \
arch/xtensa/kernel/syscalls/syscall.tbl,linux-user/xtensa/syscall.tbl\
"

linux="$1"
output="$2"

if [ -z "$linux" ] || ! [ -d "$linux" ]; then
    cat << EOF
usage: update-syscalltbl.sh LINUX_PATH [OUTPUT_PATH]

LINUX_PATH      Linux kernel directory to obtain the syscall.tbl from
OUTPUT_PATH     output directory, usually the qemu source tree (default: $PWD)
EOF
    exit 1
fi

if [ -z "$output" ]; then
    output="$PWD"
fi

for entry in $TBL_LIST; do
    OFS="$IFS"
    IFS=,
    set $entry
    src=$1
    dst=$2
    IFS="$OFS"
    if ! cp "$linux/$src" "$output/$dst" ; then
        echo "Cannot copy $linux/$src to $output/$dst" 1>&2
        exit 1
    fi
done

