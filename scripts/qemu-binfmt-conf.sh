#!/bin/sh
# Enable automatic program execution by the kernel.

qemu_target_list="i386 i486 alpha arm armeb sparc sparc32plus sparc64 \
ppc ppc64 ppc64le m68k mips mipsel mipsn32 mipsn32el mips64 mips64el \
sh4 sh4eb s390x aarch64 aarch64_be hppa riscv32 riscv64 xtensa xtensaeb \
microblaze microblazeel or1k x86_64 hexagon loongarch64"

i386_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x03\x00'
i386_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
i386_family=i386

i486_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x06\x00'
i486_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
i486_family=i386

x86_64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x3e\x00'
x86_64_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
x86_64_family=i386

alpha_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x26\x90'
alpha_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
alpha_family=alpha

arm_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00'
arm_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
arm_family=arm

armeb_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28'
armeb_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
armeb_family=armeb

sparc_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x02'
sparc_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sparc_family=sparc

sparc32plus_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x12'
sparc32plus_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sparc32plus_family=sparc

sparc64_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x2b'
sparc64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sparc64_family=sparc

ppc_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x14'
ppc_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
ppc_family=ppc

ppc64_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x15'
ppc64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
ppc64_family=ppc

ppc64le_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x15\x00'
ppc64le_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\x00'
ppc64le_family=ppcle

m68k_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x04'
m68k_mask='\xff\xff\xff\xff\xff\xff\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
m68k_family=m68k

# FIXME: We could use the other endianness on a MIPS host.

mips_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
mips_mask='\xff\xff\xff\xff\xff\xff\xff\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x20'
mips_family=mips

mipsel_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
mipsel_mask='\xff\xff\xff\xff\xff\xff\xff\x00\x00\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x20\x00\x00\x00'
mipsel_family=mips

mipsn32_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x20'
mipsn32_mask='\xff\xff\xff\xff\xff\xff\xff\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x20'
mipsn32_family=mips

mipsn32el_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x20\x00\x00\x00'
mipsn32el_mask='\xff\xff\xff\xff\xff\xff\xff\x00\x00\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x20\x00\x00\x00'
mipsn32el_family=mips

mips64_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08'
mips64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
mips64_family=mips

mips64el_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00'
mips64el_mask='\xff\xff\xff\xff\xff\xff\xff\x00\x00\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
mips64el_family=mips

sh4_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x2a\x00'
sh4_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
sh4_family=sh4

sh4eb_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x2a'
sh4eb_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sh4eb_family=sh4

s390x_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x16'
s390x_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
s390x_family=s390x

aarch64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00'
aarch64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
aarch64_family=arm

aarch64_be_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7'
aarch64_be_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
aarch64_be_family=armeb

hppa_magic='\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x0f'
hppa_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
hppa_family=hppa

riscv32_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xf3\x00'
riscv32_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
riscv32_family=riscv

riscv64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xf3\x00'
riscv64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
riscv64_family=riscv

xtensa_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x5e\x00'
xtensa_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
xtensa_family=xtensa

xtensaeb_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x5e'
xtensaeb_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
xtensaeb_family=xtensaeb

microblaze_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xba\xab'
microblaze_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
microblaze_family=microblaze

microblazeel_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xab\xba'
microblazeel_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
microblazeel_family=microblazeel

or1k_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x5c'
or1k_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
or1k_family=or1k

hexagon_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xa4\x00'
hexagon_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
hexagon_family=hexagon

loongarch64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x02\x01'
loongarch64_mask='\xff\xff\xff\xff\xff\xff\xff\xfc\x00\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
loongarch64_family=loongarch

# Converts the name of a host CPU architecture to the corresponding QEMU
# target.
#
# FIXME: This can probably be simplified a lot by dropping most entries.
#        Remember that the script is only used on Linux, so we only need to
#        handle the strings Linux uses to report the host CPU architecture.
qemu_normalize() {
    cpu="$1"
    case "$cpu" in
    i[3-6]86)
        echo "i386"
        ;;
    amd64)
        echo "x86_64"
        ;;
    powerpc)
        echo "ppc"
        ;;
    ppc64el)
        echo "ppc64le"
        ;;
    armel|armhf|armv[4-9]*l)
        echo "arm"
        ;;
    armv[4-9]*b)
        echo "armeb"
        ;;
    arm64)
        echo "aarch64"
        ;;
    *)
        echo "$cpu"
        ;;
    esac
}

usage() {
    cat <<EOF
Usage: qemu-binfmt-conf.sh [--qemu-path PATH][--debian][--systemd CPU]
                           [--help][--credential yes|no][--exportdir PATH]
                           [--persistent yes|no][--qemu-suffix SUFFIX]
                           [--preserve-argv0 yes|no]

       Configure binfmt_misc to use qemu interpreter

       --help:          display this usage
       --qemu-path:     set path to qemu interpreter ($QEMU_PATH)
       --qemu-suffix:   add a suffix to the default interpreter name
       --debian:        don't write into /proc,
                        instead generate update-binfmts templates
       --systemd:       don't write into /proc,
                        instead generate file for systemd-binfmt.service
                        for the given CPU. If CPU is "ALL", generate a
                        file for all known cpus
       --exportdir:     define where to write configuration files
                        (default: $SYSTEMDDIR or $DEBIANDIR)
       --credential:    if yes, credential and security tokens are
                        calculated according to the binary to interpret
       --persistent:    if yes, the interpreter is loaded when binfmt is
                        configured and remains in memory. All future uses
                        are cloned from the open file.
       --ignore-family: if yes, it is assumed that the host CPU (e.g. riscv64)
                        can't natively run programs targeting a CPU that is
                        part of the same family (e.g. riscv32).
       --preserve-argv0 preserve argv[0]

    To import templates with update-binfmts, use :

        sudo update-binfmts --importdir ${EXPORTDIR:-$DEBIANDIR} --import qemu-CPU

    To remove interpreter, use :

        sudo update-binfmts --package qemu-CPU --remove qemu-CPU $QEMU_PATH

    With systemd, binfmt files are loaded by systemd-binfmt.service

    The environment variable HOST_ARCH allows to override 'uname' to generate
    configuration files for a different architecture than the current one.

    where CPU is one of:

        $qemu_target_list

EOF
}

qemu_check_access() {
    if [ ! -w "$1" ] ; then
        echo "ERROR: cannot write to $1" 1>&2
        exit 1
    fi
}

qemu_check_bintfmt_misc() {
    # load the binfmt_misc module
    if [ ! -d /proc/sys/fs/binfmt_misc ]; then
      if ! /sbin/modprobe binfmt_misc ; then
          exit 1
      fi
    fi
    if [ ! -f /proc/sys/fs/binfmt_misc/register ]; then
      if ! mount binfmt_misc -t binfmt_misc /proc/sys/fs/binfmt_misc ; then
          exit 1
      fi
    fi

    qemu_check_access /proc/sys/fs/binfmt_misc/register
}

installed_dpkg() {
    dpkg --status "$1" > /dev/null 2>&1
}

qemu_check_debian() {
    if [ ! -e /etc/debian_version ] ; then
        echo "WARNING: your system is not a Debian based distro" 1>&2
    elif ! installed_dpkg binfmt-support ; then
        echo "WARNING: package binfmt-support is needed" 1>&2
    fi
    qemu_check_access "$EXPORTDIR"
}

qemu_check_systemd() {
    if ! systemctl -q is-enabled systemd-binfmt.service ; then
        echo "WARNING: systemd-binfmt.service is missing or disabled" 1>&2
    fi
    qemu_check_access "$EXPORTDIR"
}

qemu_generate_register() {
    flags=""
    if [ "$CREDENTIAL" = "yes" ] ; then
        flags="OC"
    fi
    if [ "$PERSISTENT" = "yes" ] ; then
        flags="${flags}F"
    fi
    if [ "$PRESERVE_ARG0" = "yes" ] ; then
        flags="${flags}P"
    fi

    echo ":qemu-$cpu:M::$magic:$mask:$qemu:$flags"
}

qemu_register_interpreter() {
    echo "Setting $qemu as binfmt interpreter for $cpu"
    qemu_generate_register > /proc/sys/fs/binfmt_misc/register
}

qemu_generate_systemd() {
    echo "Setting $qemu as binfmt interpreter for $cpu for systemd-binfmt.service"
    qemu_generate_register > "$EXPORTDIR/qemu-$cpu.conf"
}

qemu_generate_debian() {
    cat > "$EXPORTDIR/qemu-$cpu" <<EOF
package qemu-$cpu
interpreter $qemu
magic $magic
mask $mask
credentials $CREDENTIAL
preserve $PRESERVE_ARG0
fix_binary $PERSISTENT
EOF
}

qemu_set_binfmts() {
    # probe cpu type
    host_cpu=$(qemu_normalize ${HOST_ARCH:-$(uname -m)})
    host_family=$(eval echo \$${host_cpu}_family)

    if [ "$host_family" = "" ] ; then
        echo "INTERNAL ERROR: unknown host cpu $host_cpu" 1>&2
        exit 1
    fi

    # register the interpreter for each cpu except for the native one

    for cpu in ${qemu_target_list} ; do
        magic=$(eval echo \$${cpu}_magic)
        mask=$(eval echo \$${cpu}_mask)
        family=$(eval echo \$${cpu}_family)

        target="$cpu"
        if [ "$cpu" = "i486" ] ; then
            target="i386"
        fi

        qemu="$QEMU_PATH/qemu-$target$QEMU_SUFFIX"

        if [ "$magic" = "" ] || [ "$mask" = "" ] || [ "$family" = "" ] ; then
            echo "INTERNAL ERROR: unknown cpu $cpu" 1>&2
            continue
        fi

        if [ "$host_family" = "$family" ] ; then
            # When --ignore-family is used, we have to generate rules even
            # for targets that are in the same family as the host CPU. The
            # only exception is of course when the CPU types exactly match
            if [ "$target" = "$host_cpu" ] || [ "$IGNORE_FAMILY" = "no" ] ; then
                continue
            fi
        fi

        $BINFMT_SET
    done
}

CHECK=qemu_check_bintfmt_misc
BINFMT_SET=qemu_register_interpreter

SYSTEMDDIR="/etc/binfmt.d"
DEBIANDIR="/usr/share/binfmts"

QEMU_PATH=/usr/local/bin
CREDENTIAL=no
PERSISTENT=no
PRESERVE_ARG0=no
QEMU_SUFFIX=""
IGNORE_FAMILY=no

_longopts="debian,systemd:,qemu-path:,qemu-suffix:,exportdir:,help,credential:,\
persistent:,preserve-argv0:,ignore-family:"
options=$(getopt -o ds:Q:S:e:hc:p:g:F:i: -l ${_longopts} -- "$@")
eval set -- "$options"

while true ; do
    case "$1" in
    -d|--debian)
        CHECK=qemu_check_debian
        BINFMT_SET=qemu_generate_debian
        EXPORTDIR=${EXPORTDIR:-$DEBIANDIR}
        ;;
    -s|--systemd)
        CHECK=qemu_check_systemd
        BINFMT_SET=qemu_generate_systemd
        EXPORTDIR=${EXPORTDIR:-$SYSTEMDDIR}
        shift
        # check given cpu is in the supported CPU list
        if [ "$1" != "ALL" ] ; then
            for cpu in ${qemu_target_list} ; do
                if [ "$cpu" = "$1" ] ; then
                    break
                fi
            done

            if [ "$cpu" = "$1" ] ; then
                qemu_target_list="$1"
            else
                echo "ERROR: unknown CPU \"$1\"" 1>&2
                usage
                exit 1
            fi
        fi
        ;;
    -Q|--qemu-path)
        shift
        QEMU_PATH="$1"
        ;;
    -F|--qemu-suffix)
        shift
        QEMU_SUFFIX="$1"
        ;;
    -e|--exportdir)
        shift
        EXPORTDIR="$1"
        ;;
    -h|--help)
        usage
        exit 1
        ;;
    -c|--credential)
        shift
        CREDENTIAL="$1"
        ;;
    -p|--persistent)
        shift
        PERSISTENT="$1"
        ;;
    -g|--preserve-argv0)
        shift
        PRESERVE_ARG0="$1"
        ;;
    -i|--ignore-family)
        shift
        IGNORE_FAMILY="$1"
        ;;
    *)
        break
        ;;
    esac
    shift
done

$CHECK
qemu_set_binfmts
