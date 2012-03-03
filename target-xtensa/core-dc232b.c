#include "cpu.h"
#include "exec-all.h"
#include "gdbstub.h"
#include "host-utils.h"

#include "core-dc232b/core-isa.h"
#include "overlay_tool.h"

static const XtensaConfig dc232b = {
    .name = "dc232b",
    .options = XTENSA_OPTIONS,
    .gdb_regmap = {
        .num_regs = 120,
        .num_core_regs = 52,
        .reg = {
#include "core-dc232b/gdb-config.c"
        }
    },
    .nareg = XCHAL_NUM_AREGS,
    .ndepc = 1,
    EXCEPTIONS_SECTION,
    INTERRUPTS_SECTION,
    TLB_SECTION,
    DEBUG_SECTION,
    .clock_freq_khz = 10000,
};

REGISTER_CORE(dc232b)
