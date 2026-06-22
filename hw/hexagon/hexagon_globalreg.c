/*
 * Hexagon Global Registers
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/core/resettable.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"
#include "target/hexagon/hex_regs.h"
#include "qemu/log.h"
#include "trace.h"
#include "qapi/error.h"

#define IMMUTABLE (~0)
#define INVALID_REG_VAL 0xdeadbeef

static const char *hex_sreg_names[] = {
    [HEX_SREG_SGP0] = "sgp0",
    [HEX_SREG_SGP1] = "sgp1",
    [HEX_SREG_STID] = "stid",
    [HEX_SREG_ELR] = "elr",
    [HEX_SREG_BADVA0] = "badva0",
    [HEX_SREG_BADVA1] = "badva1",
    [HEX_SREG_SSR] = "ssr",
    [HEX_SREG_CCR] = "ccr",
    [HEX_SREG_HTID] = "htid",
    [HEX_SREG_BADVA] = "badva",
    [HEX_SREG_IMASK] = "imask",
    [HEX_SREG_GEVB] = "gevb",
    [HEX_SREG_EVB] = "evb",
    [HEX_SREG_MODECTL] = "modectl",
    [HEX_SREG_SYSCFG] = "syscfg",
    [HEX_SREG_IPENDAD] = "ipendad",
    [HEX_SREG_VID] = "vid",
    [HEX_SREG_VID1] = "vid1",
    [HEX_SREG_BESTWAIT] = "bestwait",
    [HEX_SREG_IEL] = "iel",
    [HEX_SREG_SCHEDCFG] = "schedcfg",
    [HEX_SREG_IAHL] = "iahl",
    [HEX_SREG_CFGBASE] = "cfgbase",
    [HEX_SREG_DIAG] = "diag",
    [HEX_SREG_REV] = "rev",
    [HEX_SREG_PCYCLELO] = "pcyclelo",
    [HEX_SREG_PCYCLEHI] = "pcyclehi",
    [HEX_SREG_ISDBST] = "isdbst",
    [HEX_SREG_ISDBCFG0] = "isdbcfg0",
    [HEX_SREG_ISDBCFG1] = "isdbcfg1",
    [HEX_SREG_LIVELOCK] = "livelock",
    [HEX_SREG_BRKPTPC0] = "brkptpc0",
    [HEX_SREG_BRKPTCFG0] = "brkptcfg0",
    [HEX_SREG_BRKPTPC1] = "brkptpc1",
    [HEX_SREG_BRKPTCFG1] = "brkptcfg1",
    [HEX_SREG_ISDBMBXIN] = "isdbmbxin",
    [HEX_SREG_ISDBMBXOUT] = "isdbmbxout",
    [HEX_SREG_ISDBEN] = "isdben",
    [HEX_SREG_ISDBGPR] = "isdbgpr",
    [HEX_SREG_PMUCNT4] = "pmucnt4",
    [HEX_SREG_PMUCNT5] = "pmucnt5",
    [HEX_SREG_PMUCNT6] = "pmucnt6",
    [HEX_SREG_PMUCNT7] = "pmucnt7",
    [HEX_SREG_PMUCNT0] = "pmucnt0",
    [HEX_SREG_PMUCNT1] = "pmucnt1",
    [HEX_SREG_PMUCNT2] = "pmucnt2",
    [HEX_SREG_PMUCNT3] = "pmucnt3",
    [HEX_SREG_PMUEVTCFG] = "pmuevtcfg",
    [HEX_SREG_PMUSTID0] = "pmustid0",
    [HEX_SREG_PMUEVTCFG1] = "pmuevtcfg1",
    [HEX_SREG_PMUSTID1] = "pmustid1",
    [HEX_SREG_TIMERLO] = "timerlo",
    [HEX_SREG_TIMERHI] = "timerhi",
    [HEX_SREG_PMUCFG] = "pmucfg",
    [HEX_SREG_S59] = "s59",
    [HEX_SREG_S60] = "s60",
    [HEX_SREG_S61] = "s61",
    [HEX_SREG_S62] = "s62",
    [HEX_SREG_S63] = "s63",
};

static const char *get_sreg_name(uint32_t reg)
{
    if (reg < ARRAY_SIZE(hex_sreg_names) && hex_sreg_names[reg]) {
        return hex_sreg_names[reg];
    }
    return "UNKNOWN";
}

/* Global system register mutability masks */
static const uint32_t global_sreg_immut_masks[NUM_SREGS] = {
    [HEX_SREG_EVB] = 0x000000ff,
    [HEX_SREG_MODECTL] = IMMUTABLE,
    [HEX_SREG_SYSCFG] = 0x80001c00,
    [HEX_SREG_IPENDAD] = IMMUTABLE,
    [HEX_SREG_VID] = 0xfc00fc00,
    [HEX_SREG_VID1] = 0xfc00fc00,
    [HEX_SREG_BESTWAIT] = 0xfffffe00,
    [HEX_SREG_IAHL] = 0x00000000,
    [HEX_SREG_SCHEDCFG] = 0xfffffee0,
    [HEX_SREG_CFGBASE] = IMMUTABLE,
    [HEX_SREG_DIAG] = 0x00000000,
    [HEX_SREG_REV] = IMMUTABLE,
    [HEX_SREG_ISDBST] = IMMUTABLE,
    [HEX_SREG_ISDBCFG0] = 0xe0000000,
    [HEX_SREG_BRKPTPC0] = 0x00000003,
    [HEX_SREG_BRKPTCFG0] = 0xfc007000,
    [HEX_SREG_BRKPTPC1] = 0x00000003,
    [HEX_SREG_BRKPTCFG1] = 0xfc007000,
    [HEX_SREG_ISDBMBXIN] = IMMUTABLE,
    [HEX_SREG_ISDBMBXOUT] = 0x00000000,
    [HEX_SREG_ISDBEN] = 0xfffffffe,
    [HEX_SREG_TIMERLO] = IMMUTABLE,
    [HEX_SREG_TIMERHI] = IMMUTABLE,
};

static void hexagon_globalreg_init(Object *obj)
{
    HexagonGlobalRegState *s = HEXAGON_GLOBALREG(obj);

    memset(s->regs, 0, sizeof(s->regs));
}

static inline uint32_t apply_write_mask(uint32_t new_val, uint32_t cur_val,
                                        uint32_t reg_mask)
{
    if (reg_mask) {
        return (new_val & ~reg_mask) | (cur_val & reg_mask);
    }
    return new_val;
}

uint32_t hexagon_globalreg_read(HexagonGlobalRegState *s, uint32_t reg,
                                uint32_t htid)
{
    uint32_t value;

    if (!s) {
        return 0;
    }
    g_assert(reg < NUM_SREGS);
    g_assert(reg >= HEX_SREG_GLB_START);

    value = s->regs[reg];

    trace_hexagon_globalreg_read(htid, get_sreg_name(reg), value);
    return value;
}

void hexagon_globalreg_write(HexagonGlobalRegState *s, uint32_t reg,
                             uint32_t value, uint32_t htid)
{
    if (!s) {
        return;
    }
    g_assert(reg < NUM_SREGS);
    g_assert(reg >= HEX_SREG_GLB_START);
    s->regs[reg] = value;
    trace_hexagon_globalreg_write(htid, get_sreg_name(reg), value);
}

uint32_t hexagon_globalreg_masked_value(HexagonGlobalRegState *s, uint32_t reg,
                                        uint32_t value)
{
    uint32_t reg_mask;

    if (!s) {
        return value;
    }
    g_assert(reg < NUM_SREGS);
    g_assert(reg >= HEX_SREG_GLB_START);
    reg_mask = global_sreg_immut_masks[reg];
    return reg_mask == IMMUTABLE ?
            s->regs[reg] :
            apply_write_mask(value, s->regs[reg], reg_mask);
}

void hexagon_globalreg_write_masked(HexagonGlobalRegState *s, uint32_t reg,
                                    uint32_t value)
{
    if (!s) {
        return;
    }
    s->regs[reg] = hexagon_globalreg_masked_value(s, reg, value);
}

uint64_t hexagon_globalreg_get_pcycle_base(HexagonGlobalRegState *s)
{
    g_assert(s);
    return s->g_pcycle_base;
}

void hexagon_globalreg_set_pcycle_base(HexagonGlobalRegState *s,
                                       uint64_t value)
{
    g_assert(s);
    s->g_pcycle_base = value;
}

static void do_hexagon_globalreg_reset(HexagonGlobalRegState *s)
{
    uint32_t isdben_val = 0;

    g_assert(s);
    memset(s->regs, 0, sizeof(s->regs));

    s->g_pcycle_base = 0;

    s->regs[HEX_SREG_EVB] = s->boot_evb;
    s->regs[HEX_SREG_CFGBASE] = HEXAGON_CFG_ADDR_BASE(s->config_table_addr);
    s->regs[HEX_SREG_REV] = s->dsp_rev;

    if (s->isdben_etm_enable) {
        isdben_val |= (1 << 0);  /* ETM enable bit */
    }
    if (s->isdben_dfd_enable) {
        isdben_val |= (1 << 1);  /* DFD enable bit */
    }
    if (s->isdben_trusted) {
        isdben_val |= (1 << 2);  /* Trusted bit */
    }
    if (s->isdben_secure) {
        isdben_val |= (1 << 3);  /* Secure bit */
    }
    s->regs[HEX_SREG_ISDBEN] = isdben_val;
    s->regs[HEX_SREG_MODECTL] = 0x1;

    /*
     * These register indices are placeholders in these arrays
     * and their actual values are synthesized from state elsewhere.
     * We can initialize these with invalid values so that if we
     * mistakenly generate reads, they will look obviously wrong.
     */
    s->regs[HEX_SREG_PCYCLELO] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PCYCLEHI] = INVALID_REG_VAL;
    s->regs[HEX_SREG_TIMERLO] = INVALID_REG_VAL;
    s->regs[HEX_SREG_TIMERHI] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT0] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT1] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT2] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT3] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT4] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT5] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT6] = INVALID_REG_VAL;
    s->regs[HEX_SREG_PMUCNT7] = INVALID_REG_VAL;
}

static void hexagon_globalreg_reset_hold(Object *obj, ResetType type)
{
    HexagonGlobalRegState *s = HEXAGON_GLOBALREG(obj);
    do_hexagon_globalreg_reset(s);
}

static const VMStateDescription vmstate_hexagon_globalreg = {
    .name = "hexagon_globalreg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_UINT32_ARRAY(regs, HexagonGlobalRegState, NUM_SREGS),
        VMSTATE_UINT64(g_pcycle_base, HexagonGlobalRegState),
        VMSTATE_UINT32(boot_evb, HexagonGlobalRegState),
        VMSTATE_UINT64(config_table_addr, HexagonGlobalRegState),
        VMSTATE_UINT32(dsp_rev, HexagonGlobalRegState),
        VMSTATE_BOOL(isdben_etm_enable, HexagonGlobalRegState),
        VMSTATE_BOOL(isdben_dfd_enable, HexagonGlobalRegState),
        VMSTATE_BOOL(isdben_trusted, HexagonGlobalRegState),
        VMSTATE_BOOL(isdben_secure, HexagonGlobalRegState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property hexagon_globalreg_properties[] = {
    DEFINE_PROP_UINT32("boot-evb", HexagonGlobalRegState, boot_evb, 0x0),
    DEFINE_PROP_UINT64("config-table-addr", HexagonGlobalRegState,
                       config_table_addr, 0xffffffffULL),
    DEFINE_PROP_UINT32("dsp-rev", HexagonGlobalRegState, dsp_rev, 0),
    DEFINE_PROP_BOOL("isdben-etm-enable", HexagonGlobalRegState,
                     isdben_etm_enable, false),
    DEFINE_PROP_BOOL("isdben-dfd-enable", HexagonGlobalRegState,
                     isdben_dfd_enable, false),
    DEFINE_PROP_BOOL("isdben-trusted", HexagonGlobalRegState,
                     isdben_trusted, false),
    DEFINE_PROP_BOOL("isdben-secure", HexagonGlobalRegState,
                     isdben_secure, false),
};

static void hexagon_globalreg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = hexagon_globalreg_reset_hold;
    dc->vmsd = &vmstate_hexagon_globalreg;
    dc->user_creatable = false;
    device_class_set_props(dc, hexagon_globalreg_properties);
}

static const TypeInfo hexagon_globalreg_info = {
    .name = TYPE_HEXAGON_GLOBALREG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonGlobalRegState),
    .instance_init = hexagon_globalreg_init,
    .class_init = hexagon_globalreg_class_init,
};

static void hexagon_globalreg_register_types(void)
{
    type_register_static(&hexagon_globalreg_info);
}

type_init(hexagon_globalreg_register_types)
