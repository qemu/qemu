/*
 * SiFive U series machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
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

#ifndef HW_SIFIVE_U_H
#define HW_SIFIVE_U_H

#include "hw/net/cadence_gem.h"

#define TYPE_RISCV_U_SOC "riscv.sifive.u.soc"
#define RISCV_U_SOC(obj) \
    OBJECT_CHECK(SiFiveUSoCState, (obj), TYPE_RISCV_U_SOC)

typedef struct SiFiveUSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    CadenceGEMState gem;
} SiFiveUSoCState;

typedef struct SiFiveUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    SiFiveUSoCState soc;
    void *fdt;
    int fdt_size;
} SiFiveUState;

enum {
    SIFIVE_U_DEBUG,
    SIFIVE_U_MROM,
    SIFIVE_U_CLINT,
    SIFIVE_U_PLIC,
    SIFIVE_U_UART0,
    SIFIVE_U_UART1,
    SIFIVE_U_DRAM,
    SIFIVE_U_GEM
};

enum {
    SIFIVE_U_UART0_IRQ = 3,
    SIFIVE_U_UART1_IRQ = 4,
    SIFIVE_U_GEM_IRQ = 0x35
};

enum {
    SIFIVE_U_CLOCK_FREQ = 1000000000,
    SIFIVE_U_GEM_CLOCK_FREQ = 125000000
};

#define SIFIVE_U_PLIC_HART_CONFIG "MS"
#define SIFIVE_U_PLIC_NUM_SOURCES 54
#define SIFIVE_U_PLIC_NUM_PRIORITIES 7
#define SIFIVE_U_PLIC_PRIORITY_BASE 0x04
#define SIFIVE_U_PLIC_PENDING_BASE 0x1000
#define SIFIVE_U_PLIC_ENABLE_BASE 0x2000
#define SIFIVE_U_PLIC_ENABLE_STRIDE 0x80
#define SIFIVE_U_PLIC_CONTEXT_BASE 0x200000
#define SIFIVE_U_PLIC_CONTEXT_STRIDE 0x1000

#if defined(TARGET_RISCV32)
#define SIFIVE_U_CPU TYPE_RISCV_CPU_SIFIVE_U34
#elif defined(TARGET_RISCV64)
#define SIFIVE_U_CPU TYPE_RISCV_CPU_SIFIVE_U54
#endif

#endif
