#! /bin/bash -e

OVERLAY="$1"
NAME="$2"
FREQ=40000
BASE=$(dirname "$0")
TARGET="$BASE"/core-$NAME

[ $# -ge 2 -a -f "$OVERLAY" ] || { cat <<EOF
Usage: $0 overlay-archive-to-import core-name [frequency-in-KHz]
    overlay-archive-to-import:  file name of xtensa-config-overlay.tar.gz
                                to import configuration from.
    core-name:                  QEMU name of the imported core. Must be valid
                                C identifier.
    frequency-in-KHz:           core frequency (40MHz if not specified).
EOF
exit
}

[ $# -ge 3 ] && FREQ="$3"
mkdir -p "$TARGET"
tar -xf "$OVERLAY" -C "$TARGET" --strip-components=2 \
    xtensa/config/core-isa.h \
    xtensa/config/core-matmap.h
tar -xf "$OVERLAY" -O gdb/xtensa-config.c | \
    sed -n '1,/*\//p;/XTREG/,/XTREG_END/p' > "$TARGET"/gdb-config.c.inc
#
# Fix up known issues in the xtensa-modules.c
#
tar -xf "$OVERLAY" -O binutils/xtensa-modules.c | \
    sed -e 's/^\(xtensa_opcode_encode_fn.*\[\] =\)/static \1/' \
        -e '/^int num_bypass_groups()/,/}/d' \
        -e '/^int num_bypass_group_chunks()/,/}/d' \
        -e '/^uint32 \*bypass_entry(int i)/,/}/d' \
        -e '/^#include "ansidecl.h"/d' \
        -e '/^Slot_[a-zA-Z0-9_]\+_decode (const xtensa_insnbuf insn)/,/^}/s/^  return 0;$/  return XTENSA_UNDEFINED;/' \
        -e 's/#include <xtensa-isa.h>/#include "xtensa-isa.h"/' \
        -e 's/^\(xtensa_isa_internal xtensa_modules\)/static \1/' \
    > "$TARGET"/xtensa-modules.c.inc

cat <<EOF > "${TARGET}.c"
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu/host-utils.h"

#include "core-$NAME/core-isa.h"
#include "core-$NAME/core-matmap.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_$NAME
#include "core-$NAME/xtensa-modules.c.inc"

static XtensaConfig $NAME __attribute__((unused)) = {
    .name = "$NAME",
    .gdb_regmap = {
        .reg = {
#include "core-$NAME/gdb-config.c.inc"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = $FREQ,
    DEFAULT_SECTIONS
};

REGISTER_CORE($NAME)
EOF

grep -qxf core-${NAME}.c "$BASE"/cores.list || \
    echo core-${NAME}.c >> "$BASE"/cores.list
