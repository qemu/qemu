/*
 * QEMU RISC-V Native Debug Support
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_DEBUG_H
#define RISCV_DEBUG_H

/* trigger indexes implemented */
enum {
    TRIGGER_TYPE2_IDX_0 = 0,
    TRIGGER_TYPE2_IDX_1,
    TRIGGER_TYPE2_NUM,
    TRIGGER_NUM = TRIGGER_TYPE2_NUM
};

/* register index of tdata CSRs */
enum {
    TDATA1 = 0,
    TDATA2,
    TDATA3,
    TDATA_NUM
};

typedef enum {
    TRIGGER_TYPE_NO_EXIST = 0,      /* trigger does not exist */
    TRIGGER_TYPE_AD_MATCH = 2,      /* address/data match trigger */
    TRIGGER_TYPE_INST_CNT = 3,      /* instruction count trigger */
    TRIGGER_TYPE_INT = 4,           /* interrupt trigger */
    TRIGGER_TYPE_EXCP = 5,          /* exception trigger */
    TRIGGER_TYPE_AD_MATCH6 = 6,     /* new address/data match trigger */
    TRIGGER_TYPE_EXT_SRC = 7,       /* external source trigger */
    TRIGGER_TYPE_UNAVAIL = 15       /* trigger exists, but unavailable */
} trigger_type_t;

typedef struct {
    target_ulong mcontrol;
    target_ulong maddress;
    struct CPUBreakpoint *bp;
    struct CPUWatchpoint *wp;
} type2_trigger_t;

/* tdata field masks */

#define RV32_TYPE(t)    ((uint32_t)(t) << 28)
#define RV32_TYPE_MASK  (0xf << 28)
#define RV32_DMODE      BIT(27)
#define RV64_TYPE(t)    ((uint64_t)(t) << 60)
#define RV64_TYPE_MASK  (0xfULL << 60)
#define RV64_DMODE      BIT_ULL(59)

/* mcontrol field masks */

#define TYPE2_LOAD      BIT(0)
#define TYPE2_STORE     BIT(1)
#define TYPE2_EXEC      BIT(2)
#define TYPE2_U         BIT(3)
#define TYPE2_S         BIT(4)
#define TYPE2_M         BIT(6)
#define TYPE2_MATCH     (0xf << 7)
#define TYPE2_CHAIN     BIT(11)
#define TYPE2_ACTION    (0xf << 12)
#define TYPE2_SIZELO    (0x3 << 16)
#define TYPE2_TIMING    BIT(18)
#define TYPE2_SELECT    BIT(19)
#define TYPE2_HIT       BIT(20)
#define TYPE2_SIZEHI    (0x3 << 21) /* RV64 only */

/* access size */
enum {
    SIZE_ANY = 0,
    SIZE_1B,
    SIZE_2B,
    SIZE_4B,
    SIZE_6B,
    SIZE_8B,
    SIZE_10B,
    SIZE_12B,
    SIZE_14B,
    SIZE_16B,
    SIZE_NUM = 16
};

bool tdata_available(CPURISCVState *env, int tdata_index);

target_ulong tselect_csr_read(CPURISCVState *env);
void tselect_csr_write(CPURISCVState *env, target_ulong val);

target_ulong tdata_csr_read(CPURISCVState *env, int tdata_index);
void tdata_csr_write(CPURISCVState *env, int tdata_index, target_ulong val);

#endif /* RISCV_DEBUG_H */
