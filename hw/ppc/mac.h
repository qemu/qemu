/*
 * QEMU PowerMac emulation shared definitions and prototypes
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
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
#if !defined(__PPC_MAC_H__)
#define __PPC_MAC_H__

#include "exec/memory.h"
#include "hw/sysbus.h"
#include "hw/ide/internal.h"
#include "hw/input/adb.h"

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define BIOS_SIZE     (1024 * 1024)
#define NVRAM_SIZE        0x2000
#define PROM_FILENAME    "openbios-ppc"
#define PROM_ADDR         0xfff00000

#define KERNEL_LOAD_ADDR 0x01000000
#define KERNEL_GAP       0x00100000

#define ESCC_CLOCK 3686400

/* Cuda */
#define TYPE_CUDA "cuda"
#define CUDA(obj) OBJECT_CHECK(CUDAState, (obj), TYPE_CUDA)

/**
 * CUDATimer:
 * @counter_value: counter value at load time
 */
typedef struct CUDATimer {
    int index;
    uint16_t latch;
    uint16_t counter_value;
    int64_t load_time;
    int64_t next_irq_time;
    QEMUTimer *timer;
} CUDATimer;

/**
 * CUDAState:
 * @b: B-side data
 * @a: A-side data
 * @dirb: B-side direction (1=output)
 * @dira: A-side direction (1=output)
 * @sr: Shift register
 * @acr: Auxiliary control register
 * @pcr: Peripheral control register
 * @ifr: Interrupt flag register
 * @ier: Interrupt enable register
 * @anh: A-side data, no handshake
 * @last_b: last value of B register
 * @last_acr: last value of ACR register
 */
typedef struct CUDAState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    /* cuda registers */
    uint8_t b;
    uint8_t a;
    uint8_t dirb;
    uint8_t dira;
    uint8_t sr;
    uint8_t acr;
    uint8_t pcr;
    uint8_t ifr;
    uint8_t ier;
    uint8_t anh;

    ADBBusState adb_bus;
    CUDATimer timers[2];

    uint32_t tick_offset;

    uint8_t last_b;
    uint8_t last_acr;

    int data_in_size;
    int data_in_index;
    int data_out_index;

    qemu_irq irq;
    uint8_t autopoll;
    uint8_t data_in[128];
    uint8_t data_out[16];
    QEMUTimer *adb_poll_timer;
} CUDAState;

/* MacIO */
#define TYPE_OLDWORLD_MACIO "macio-oldworld"
#define TYPE_NEWWORLD_MACIO "macio-newworld"

#define TYPE_MACIO_IDE "macio-ide"
#define MACIO_IDE(obj) OBJECT_CHECK(MACIOIDEState, (obj), TYPE_MACIO_IDE)

typedef struct MACIOIDEState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    qemu_irq irq;
    qemu_irq dma_irq;

    MemoryRegion mem;
    IDEBus bus;
    BlockDriverAIOCB *aiocb;
    IDEDMA dma;
    void *dbdma;
    bool dma_active;
} MACIOIDEState;

void macio_ide_init_drives(MACIOIDEState *ide, DriveInfo **hd_table);
void macio_ide_register_dma(MACIOIDEState *ide, void *dbdma, int channel);

void macio_init(PCIDevice *dev,
                MemoryRegion *pic_mem,
                MemoryRegion *escc_mem);

/* Heathrow PIC */
qemu_irq *heathrow_pic_init(MemoryRegion **pmem,
                            int nb_cpus, qemu_irq **irqs);

/* Grackle PCI */
#define TYPE_GRACKLE_PCI_HOST_BRIDGE "grackle-pcihost"
PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io);

/* UniNorth PCI */
PCIBus *pci_pmac_init(qemu_irq *pic,
                      MemoryRegion *address_space_mem,
                      MemoryRegion *address_space_io);
PCIBus *pci_pmac_u3_init(qemu_irq *pic,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io);

/* Mac NVRAM */
#define TYPE_MACIO_NVRAM "macio-nvram"
#define MACIO_NVRAM(obj) \
    OBJECT_CHECK(MacIONVRAMState, (obj), TYPE_MACIO_NVRAM)

typedef struct MacIONVRAMState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint32_t size;
    uint32_t it_shift;

    MemoryRegion mem;
    uint8_t *data;
} MacIONVRAMState;

void pmac_format_nvram_partition (MacIONVRAMState *nvr, int len);
uint8_t macio_nvram_read(MacIONVRAMState *s, uint32_t addr);
void macio_nvram_write(MacIONVRAMState *s, uint32_t addr, uint8_t val);
#endif /* !defined(__PPC_MAC_H__) */
