/*
 * ARM SMMUv3 support - Internal API
 *
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_SMMU_V3_INTERNAL_H
#define HW_ARM_SMMU_V3_INTERNAL_H

#include "hw/arm/smmu-common.h"

/* MMIO Registers */

REG32(IDR0,                0x0)
    FIELD(IDR0, S1P,         1 , 1)
    FIELD(IDR0, TTF,         2 , 2)
    FIELD(IDR0, COHACC,      4 , 1)
    FIELD(IDR0, ASID16,      12, 1)
    FIELD(IDR0, TTENDIAN,    21, 2)
    FIELD(IDR0, STALL_MODEL, 24, 2)
    FIELD(IDR0, TERM_MODEL,  26, 1)
    FIELD(IDR0, STLEVEL,     27, 2)

REG32(IDR1,                0x4)
    FIELD(IDR1, SIDSIZE,      0 , 6)
    FIELD(IDR1, EVENTQS,      16, 5)
    FIELD(IDR1, CMDQS,        21, 5)

#define SMMU_IDR1_SIDSIZE 16
#define SMMU_CMDQS   19
#define SMMU_EVENTQS 19

REG32(IDR2,                0x8)
REG32(IDR3,                0xc)
REG32(IDR4,                0x10)
REG32(IDR5,                0x14)
     FIELD(IDR5, OAS,         0, 3);
     FIELD(IDR5, GRAN4K,      4, 1);
     FIELD(IDR5, GRAN16K,     5, 1);
     FIELD(IDR5, GRAN64K,     6, 1);

#define SMMU_IDR5_OAS 4

REG32(IIDR,                0x1c)
REG32(CR0,                 0x20)
    FIELD(CR0, SMMU_ENABLE,   0, 1)
    FIELD(CR0, EVENTQEN,      2, 1)
    FIELD(CR0, CMDQEN,        3, 1)

REG32(CR0ACK,              0x24)
REG32(CR1,                 0x28)
REG32(CR2,                 0x2c)
REG32(STATUSR,             0x40)
REG32(IRQ_CTRL,            0x50)
    FIELD(IRQ_CTRL, GERROR_IRQEN,        0, 1)
    FIELD(IRQ_CTRL, PRI_IRQEN,           1, 1)
    FIELD(IRQ_CTRL, EVENTQ_IRQEN,        2, 1)

REG32(IRQ_CTRL_ACK,        0x54)
REG32(GERROR,              0x60)
    FIELD(GERROR, CMDQ_ERR,           0, 1)
    FIELD(GERROR, EVENTQ_ABT_ERR,     2, 1)
    FIELD(GERROR, PRIQ_ABT_ERR,       3, 1)
    FIELD(GERROR, MSI_CMDQ_ABT_ERR,   4, 1)
    FIELD(GERROR, MSI_EVENTQ_ABT_ERR, 5, 1)
    FIELD(GERROR, MSI_PRIQ_ABT_ERR,   6, 1)
    FIELD(GERROR, MSI_GERROR_ABT_ERR, 7, 1)
    FIELD(GERROR, MSI_SFM_ERR,        8, 1)

REG32(GERRORN,             0x64)

#define A_GERROR_IRQ_CFG0  0x68 /* 64b */
REG32(GERROR_IRQ_CFG1, 0x70)
REG32(GERROR_IRQ_CFG2, 0x74)

#define A_STRTAB_BASE      0x80 /* 64b */

#define SMMU_BASE_ADDR_MASK 0xffffffffffe0

REG32(STRTAB_BASE_CFG,     0x88)
    FIELD(STRTAB_BASE_CFG, FMT,      16, 2)
    FIELD(STRTAB_BASE_CFG, SPLIT,    6 , 5)
    FIELD(STRTAB_BASE_CFG, LOG2SIZE, 0 , 6)

#define A_CMDQ_BASE        0x90 /* 64b */
REG32(CMDQ_PROD,           0x98)
REG32(CMDQ_CONS,           0x9c)
    FIELD(CMDQ_CONS, ERR, 24, 7)

#define A_EVENTQ_BASE      0xa0 /* 64b */
REG32(EVENTQ_PROD,         0xa8)
REG32(EVENTQ_CONS,         0xac)

#define A_EVENTQ_IRQ_CFG0  0xb0 /* 64b */
REG32(EVENTQ_IRQ_CFG1,     0xb8)
REG32(EVENTQ_IRQ_CFG2,     0xbc)

#define A_IDREGS           0xfd0

static inline int smmu_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->cr[0], CR0, SMMU_ENABLE);
}

/* Command Queue Entry */
typedef struct Cmd {
    uint32_t word[4];
} Cmd;

/* Event Queue Entry */
typedef struct Evt  {
    uint32_t word[8];
} Evt;

static inline uint32_t smmuv3_idreg(int regoffset)
{
    /*
     * Return the value of the Primecell/Corelink ID registers at the
     * specified offset from the first ID register.
     * These value indicate an ARM implementation of MMU600 p1
     */
    static const uint8_t smmuv3_ids[] = {
        0x04, 0, 0, 0, 0x84, 0xB4, 0xF0, 0x10, 0x0D, 0xF0, 0x05, 0xB1
    };
    return smmuv3_ids[regoffset / 4];
}

static inline bool smmuv3_eventq_irq_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->irq_ctrl, IRQ_CTRL, EVENTQ_IRQEN);
}

static inline bool smmuv3_gerror_irq_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->irq_ctrl, IRQ_CTRL, GERROR_IRQEN);
}

/* public until callers get introduced */
void smmuv3_trigger_irq(SMMUv3State *s, SMMUIrq irq, uint32_t gerror_mask);
void smmuv3_write_gerrorn(SMMUv3State *s, uint32_t gerrorn);

#endif
