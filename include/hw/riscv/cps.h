/*
 * Coherent Processing System emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef RISCV_CPS_H
#define RISCV_CPS_H

#include "hw/core/sysbus.h"
#include "hw/misc/riscv_cmgcr.h"
#include "hw/misc/riscv_cpc.h"
#include "target/riscv/cpu.h"
#include "qom/object.h"

#define TYPE_RISCV_CPS "riscv-cps"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVCPSState, RISCV_CPS)

/* The model supports up to 64 harts. */
#define MAX_HARTS 64

/* The global CM base for the boston-aia model. */
#define GLOBAL_CM_BASE 0x16100000
/* The CM block is 512 KiB. */
#define CM_SIZE (1 << 19)

/*
 * The mhartid bits has cluster at bit 16, core at bit 4, and hart at
 * bit 0.
 */

#define MHARTID_CLUSTER_SHIFT 16
#define MHARTID_CORE_SHIFT 4
#define MHARTID_HART_SHIFT 0

#define APLIC_NUM_SOURCES 0x35 /* Arbitray maximum number of interrupts. */
#define APLIC_NUM_PRIO_BITS 3
#define AIA_PLIC_M_OFFSET 0x40000
#define AIA_PLIC_M_SIZE 0x8000
#define AIA_PLIC_S_OFFSET 0x60000
#define AIA_PLIC_S_SIZE 0x8000
#define AIA_CLINT_OFFSET 0x50000

typedef struct RISCVCPSState {
    SysBusDevice parent_obj;

    uint32_t num_vp;
    uint32_t num_hart;
    uint32_t num_core;
    uint64_t gcr_base;
    char *cpu_type;

    MemoryRegion container;
    RISCVGCRState gcr;
    RISCVCPCState cpc;

    DeviceState *aplic;
    CPUState **cpus;
} RISCVCPSState;

#endif
