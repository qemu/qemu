/*
 * Hexagon Global Registers QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_GLOBALREG_H
#define HEXAGON_GLOBALREG_H

#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"

#define TYPE_HEXAGON_GLOBALREG "hexagon-globalreg"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonGlobalRegState, HEXAGON_GLOBALREG)

struct HexagonGlobalRegState {
    SysBusDevice parent_obj;

    /* Array of system registers */
    uint32_t regs[NUM_SREGS];

    /* Global performance cycle counter base */
    uint64_t g_pcycle_base;

    /* Properties for global register reset values */
    uint32_t boot_evb;           /* Boot Exception Vector Base (HEX_SREG_EVB) */
    uint64_t config_table_addr;  /* Configuration table base */
    uint32_t dsp_rev;           /* DSP revision register (HEX_SREG_REV) */

    /* ISDB properties */
    bool isdben_etm_enable;     /* ISDB ETM enable bit */
    bool isdben_dfd_enable;     /* ISDB DFD enable bit */
    bool isdben_trusted;        /* ISDB trusted mode bit */
    bool isdben_secure;         /* ISDB secure mode bit */
};

/* Public interface functions */
uint32_t hexagon_globalreg_read(HexagonGlobalRegState *s, uint32_t reg,
                                uint32_t htid);
void hexagon_globalreg_write(HexagonGlobalRegState *s, uint32_t reg,
                             uint32_t value, uint32_t htid);
uint32_t hexagon_globalreg_masked_value(HexagonGlobalRegState *s, uint32_t reg,
                                        uint32_t value);
void hexagon_globalreg_write_masked(HexagonGlobalRegState *s, uint32_t reg,
                                    uint32_t value);

/* Global performance cycle counter access */
uint64_t hexagon_globalreg_get_pcycle_base(HexagonGlobalRegState *s);
void hexagon_globalreg_set_pcycle_base(HexagonGlobalRegState *s,
                                       uint64_t value);

#endif /* HEXAGON_GLOBALREG_H */
