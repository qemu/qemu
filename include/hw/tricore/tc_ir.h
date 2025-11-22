/*
 * Infineon TriCore IR (Interrupt Router) device model
 *
 * The Interrupt Router receives service requests from peripherals
 * and routes them to CPUs or DMA based on priority and configuration.
 *
 * Copyright (c) 2024
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef HW_TRICORE_TC_IR_H
#define HW_TRICORE_TC_IR_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "target/tricore/cpu.h"

#define TYPE_TC_IR "tc-ir"
OBJECT_DECLARE_SIMPLE_TYPE(TcIrState, TC_IR)

/*
 * Service Request Control (SRC) Register layout:
 * Bits 31:26 - Reserved
 * Bit 25     - SRR (Service Request Flag, read-only status)
 * Bit 24     - CLRR (Clear Request, write 1 to clear SRR)
 * Bits 23:16 - Reserved
 * Bit 15     - Reserved
 * Bit 14     - Reserved
 * Bits 13:11 - TOS (Type of Service: 0=CPU0, 1=CPU1, 2=CPU2, 3=DMA)
 * Bit 10     - SRE (Service Request Enable)
 * Bits 9:8   - Reserved
 * Bits 7:0   - SRPN (Service Request Priority Number, 0-255)
 */
#define SRC_SRPN_MASK   0x000000FF
#define SRC_SRE         (1 << 10)
#define SRC_TOS_MASK    0x00003800
#define SRC_TOS_SHIFT   11
#define SRC_CLRR        (1 << 24)
#define SRC_SRR         (1 << 25)
#define SRC_SETR        (1 << 26)

/* Maximum number of SRC registers (service request nodes) */
#define TC_IR_MAX_SRC   1024

/* IR Register offsets (relative to IR base) */
#define IR_OITRIGLVL    0x000   /* OTGM IRQ Trigger Level */
#define IR_OITRIGCNT    0x004   /* OTGM IRQ Trigger Count */
#define IR_OITMISSLVL   0x008   /* OTGM IRQ Miss Level */
#define IR_OITMISSCNT   0x00C   /* OTGM IRQ Miss Count */

/* SRC registers start at offset 0x020 (SRC index * 4) */
#define IR_SRC_BASE     0x020

struct TcIrState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* Service Request Control registers */
    uint32_t src[TC_IR_MAX_SRC];

    /* OTGM (On-chip Test and Debug Generation Module) registers */
    uint32_t oitriglvl;
    uint32_t oitrigcnt;
    uint32_t oitmisslvl;
    uint32_t oitmisscnt;

    /* Reference to CPU for raising interrupts */
    TriCoreCPU *cpu;

    /* IRQ inputs from peripherals */
    qemu_irq *irq_inputs;
};

/* Function to raise an interrupt through the IR */
void tc_ir_set_irq(void *opaque, int n, int level);

#endif /* HW_TRICORE_TC_IR_H */
