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
tar -xf "$OVERLAY" -C "$TARGET" --strip-components=1 \
    --xform='s/core/core-isa/' config/core.h
tar -xf "$OVERLAY" -O gdb/xtensa-config.c | \
    sed -n '1,/*\//p;/XTREG/,/XTREG_END/p' > "$TARGET"/gdb-config.c

cat <<EOF > "${TARGET}.c"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "qemu/host-utils.h"

#include "core-$NAME/core-isa.h"
#include "overlay_tool.h"

static XtensaConfig $NAME __attribute__((unused)) = {
    .name = "$NAME",
    .gdb_regmap = {
        .reg = {
#include "core-$NAME/gdb-config.c"
        }
    },
    .clock_freq_khz = $FREQ,
    DEFAULT_SECTIONS
};

REGISTER_CORE($NAME)
EOF

grep -q core-${NAME}.o "$BASE"/Makefile.objs || \
    echo "obj-y += core-${NAME}.o" >> "$BASE"/Makefile.objs
