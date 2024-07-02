/*
 * PowerMac MacIO device emulation
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
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

#ifndef MACIO_H
#define MACIO_H

#include "hw/char/escc.h"
#include "hw/pci/pci_device.h"
#include "hw/ide/ide-bus.h"
#include "hw/intc/heathrow_pic.h"
#include "hw/misc/macio/cuda.h"
#include "hw/misc/macio/gpio.h"
#include "hw/misc/macio/pmu.h"
#include "hw/nvram/mac_nvram.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/ppc/openpic.h"
#include "qom/object.h"

/* Old World IRQs */
#define OLDWORLD_CUDA_IRQ      0x12
#define OLDWORLD_ESCCB_IRQ     0x10
#define OLDWORLD_ESCCA_IRQ     0xf
#define OLDWORLD_IDE0_IRQ      0xd
#define OLDWORLD_IDE0_DMA_IRQ  0x2
#define OLDWORLD_IDE1_IRQ      0xe
#define OLDWORLD_IDE1_DMA_IRQ  0x3

/* New World IRQs */
#define NEWWORLD_CUDA_IRQ      0x19
#define NEWWORLD_PMU_IRQ       0x19
#define NEWWORLD_ESCCB_IRQ     0x24
#define NEWWORLD_ESCCA_IRQ     0x25
#define NEWWORLD_IDE0_IRQ      0xd
#define NEWWORLD_IDE0_DMA_IRQ  0x2
#define NEWWORLD_IDE1_IRQ      0xe
#define NEWWORLD_IDE1_DMA_IRQ  0x3
#define NEWWORLD_EXTING_GPIO1  0x2f
#define NEWWORLD_EXTING_GPIO9  0x37

/* MacIO virtual bus */
#define TYPE_MACIO_BUS "macio-bus"
OBJECT_DECLARE_SIMPLE_TYPE(MacIOBusState, MACIO_BUS)

struct MacIOBusState {
    /*< private >*/
    BusState parent_obj;
};

/* MacIO IDE */
#define TYPE_MACIO_IDE "macio-ide"
OBJECT_DECLARE_SIMPLE_TYPE(MACIOIDEState, MACIO_IDE)

struct MACIOIDEState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t addr;
    uint32_t channel;
    qemu_irq real_ide_irq;
    qemu_irq real_dma_irq;

    MemoryRegion mem;
    IDEBus bus;
    IDEDMA dma;
    void *dbdma;
    bool dma_active;
    uint32_t timing_reg;
    uint32_t irq_reg;
};

#define MACIO_IDE_PMAC_NIRQS 2

#define MACIO_IDE_PMAC_DMA_IRQ 0
#define MACIO_IDE_PMAC_IDE_IRQ 1

void macio_ide_init_drives(MACIOIDEState *ide, DriveInfo **hd_table);
void macio_ide_register_dma(MACIOIDEState *ide);

#define TYPE_MACIO "macio"
OBJECT_DECLARE_SIMPLE_TYPE(MacIOState, MACIO)

struct MacIOState {
    /*< private >*/
    PCIDevice parent;
    /*< public >*/

    MacIOBusState macio_bus;
    MemoryRegion bar;
    CUDAState cuda;
    PMUState pmu;
    DBDMAState dbdma;
    ESCCState escc;
    uint64_t frequency;
};

#define TYPE_OLDWORLD_MACIO "macio-oldworld"
OBJECT_DECLARE_SIMPLE_TYPE(OldWorldMacIOState, OLDWORLD_MACIO)

struct OldWorldMacIOState {
    /*< private >*/
    MacIOState parent_obj;
    /*< public >*/

    HeathrowState pic;

    MacIONVRAMState nvram;
    MACIOIDEState ide[2];
};

#define TYPE_NEWWORLD_MACIO "macio-newworld"
OBJECT_DECLARE_SIMPLE_TYPE(NewWorldMacIOState, NEWWORLD_MACIO)

struct NewWorldMacIOState {
    /*< private >*/
    MacIOState parent_obj;
    /*< public >*/

    bool has_pmu;
    bool has_adb;
    OpenPICState pic;
    MACIOIDEState ide[2];
    MacIOGPIOState gpio;
};

#endif /* MACIO_H */
