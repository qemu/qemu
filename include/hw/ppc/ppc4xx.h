/*
 * QEMU PowerPC 4xx emulation shared definitions
 *
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef PPC4XX_H
#define PPC4XX_H

#include "hw/ppc/ppc.h"
#include "exec/memory.h"
#include "hw/sysbus.h"

#define TYPE_PPC4xx_PCI_HOST_BRIDGE "ppc4xx-pcihost"

/*
 * Generic DCR device
 */
#define TYPE_PPC4xx_DCR_DEVICE "ppc4xx-dcr-device"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc4xxDcrDeviceState, PPC4xx_DCR_DEVICE);
struct Ppc4xxDcrDeviceState {
    SysBusDevice parent_obj;

    PowerPCCPU *cpu;
};

void ppc4xx_dcr_register(Ppc4xxDcrDeviceState *dev, int dcrn, void *opaque,
                         dcr_read_cb dcr_read, dcr_write_cb dcr_write);
bool ppc4xx_dcr_realize(Ppc4xxDcrDeviceState *dev, PowerPCCPU *cpu,
                        Error **errp);

/* Memory Access Layer (MAL) */
#define TYPE_PPC4xx_MAL "ppc4xx-mal"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc4xxMalState, PPC4xx_MAL);
struct Ppc4xxMalState {
    Ppc4xxDcrDeviceState parent_obj;

    qemu_irq irqs[4];
    uint32_t cfg;
    uint32_t esr;
    uint32_t ier;
    uint32_t txcasr;
    uint32_t txcarr;
    uint32_t txeobisr;
    uint32_t txdeir;
    uint32_t rxcasr;
    uint32_t rxcarr;
    uint32_t rxeobisr;
    uint32_t rxdeir;
    uint32_t *txctpr;
    uint32_t *rxctpr;
    uint32_t *rcbs;
    uint8_t  txcnum;
    uint8_t  rxcnum;
};

/* Peripheral local bus arbitrer */
#define TYPE_PPC4xx_PLB "ppc4xx-plb"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc4xxPlbState, PPC4xx_PLB);
struct Ppc4xxPlbState {
    Ppc4xxDcrDeviceState parent_obj;

    uint32_t acr;
    uint32_t bear;
    uint32_t besr;
};

/* Peripheral controller */
#define TYPE_PPC4xx_EBC "ppc4xx-ebc"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc4xxEbcState, PPC4xx_EBC);
struct Ppc4xxEbcState {
    Ppc4xxDcrDeviceState parent_obj;

    uint32_t addr;
    uint32_t bcr[8];
    uint32_t bap[8];
    uint32_t bear;
    uint32_t besr0;
    uint32_t besr1;
    uint32_t cfg;
};

/* SDRAM DDR controller */
typedef struct {
    MemoryRegion ram;
    MemoryRegion container; /* used for clipping */
    hwaddr base;
    hwaddr size;
    uint32_t bcr;
} Ppc4xxSdramBank;

#define SDR0_DDR0_DDRM_ENCODE(n)  ((((unsigned long)(n)) & 0x03) << 29)
#define SDR0_DDR0_DDRM_DDR1       0x20000000
#define SDR0_DDR0_DDRM_DDR2       0x40000000

#define TYPE_PPC4xx_SDRAM_DDR "ppc4xx-sdram-ddr"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc4xxSdramDdrState, PPC4xx_SDRAM_DDR);
struct Ppc4xxSdramDdrState {
    Ppc4xxDcrDeviceState parent_obj;

    MemoryRegion *dram_mr;
    uint32_t nbanks; /* Banks to use from 4, e.g. when board has less slots */
    Ppc4xxSdramBank bank[4];
    qemu_irq irq;

    uint32_t addr;
    uint32_t besr0;
    uint32_t besr1;
    uint32_t bear;
    uint32_t cfg;
    uint32_t status;
    uint32_t rtr;
    uint32_t pmit;
    uint32_t tr;
    uint32_t ecccfg;
    uint32_t eccesr;
};

void ppc4xx_sdram_ddr_enable(Ppc4xxSdramDdrState *s);

/* SDRAM DDR2 controller */
#define TYPE_PPC4xx_SDRAM_DDR2 "ppc4xx-sdram-ddr2"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc4xxSdramDdr2State, PPC4xx_SDRAM_DDR2);
struct Ppc4xxSdramDdr2State {
    Ppc4xxDcrDeviceState parent_obj;

    MemoryRegion *dram_mr;
    uint32_t nbanks; /* Banks to use from 4, e.g. when board has less slots */
    Ppc4xxSdramBank bank[4];

    uint32_t addr;
    uint32_t mcopt2;
};

void ppc4xx_sdram_ddr2_enable(Ppc4xxSdramDdr2State *s);

#endif /* PPC4XX_H */
