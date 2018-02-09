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

#ifndef PPC_MAC_H
#define PPC_MAC_H

#include "exec/memory.h"
#include "hw/sysbus.h"
#include "hw/ide/internal.h"
#include "hw/input/adb.h"
#include "hw/misc/mos6522.h"

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define BIOS_SIZE     (1024 * 1024)
#define NVRAM_SIZE        0x2000
#define PROM_FILENAME    "openbios-ppc"
#define PROM_ADDR         0xfff00000

#define KERNEL_LOAD_ADDR 0x01000000
#define KERNEL_GAP       0x00100000

#define ESCC_CLOCK 3686400

/* CUDA commands (2nd byte) */
#define CUDA_WARM_START                0x0
#define CUDA_AUTOPOLL                  0x1
#define CUDA_GET_6805_ADDR             0x2
#define CUDA_GET_TIME                  0x3
#define CUDA_GET_PRAM                  0x7
#define CUDA_SET_6805_ADDR             0x8
#define CUDA_SET_TIME                  0x9
#define CUDA_POWERDOWN                 0xa
#define CUDA_POWERUP_TIME              0xb
#define CUDA_SET_PRAM                  0xc
#define CUDA_MS_RESET                  0xd
#define CUDA_SEND_DFAC                 0xe
#define CUDA_BATTERY_SWAP_SENSE        0x10
#define CUDA_RESET_SYSTEM              0x11
#define CUDA_SET_IPL                   0x12
#define CUDA_FILE_SERVER_FLAG          0x13
#define CUDA_SET_AUTO_RATE             0x14
#define CUDA_GET_AUTO_RATE             0x16
#define CUDA_SET_DEVICE_LIST           0x19
#define CUDA_GET_DEVICE_LIST           0x1a
#define CUDA_SET_ONE_SECOND_MODE       0x1b
#define CUDA_SET_POWER_MESSAGES        0x21
#define CUDA_GET_SET_IIC               0x22
#define CUDA_WAKEUP                    0x23
#define CUDA_TIMER_TICKLE              0x24
#define CUDA_COMBINED_FORMAT_IIC       0x25

/* Cuda */
#define TYPE_CUDA "cuda"
#define CUDA(obj) OBJECT_CHECK(CUDAState, (obj), TYPE_CUDA)

typedef struct MOS6522CUDAState MOS6522CUDAState;

typedef struct CUDAState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion mem;

    ADBBusState adb_bus;
    MOS6522CUDAState *mos6522_cuda;

    uint32_t tick_offset;
    uint64_t tb_frequency;

    uint8_t last_b;
    uint8_t last_acr;

    /* MacOS 9 is racy and requires a delay upon setting the SR_INT bit */
    uint64_t sr_delay_ns;
    QEMUTimer *sr_delay_timer;

    int data_in_size;
    int data_in_index;
    int data_out_index;

    qemu_irq irq;
    uint16_t adb_poll_mask;
    uint8_t autopoll_rate_ms;
    uint8_t autopoll;
    uint8_t data_in[128];
    uint8_t data_out[16];
    QEMUTimer *adb_poll_timer;
} CUDAState;

/* MOS6522 CUDA */
typedef struct MOS6522CUDAState {
    /*< private >*/
    MOS6522State parent_obj;

    CUDAState *cuda;
} MOS6522CUDAState;

#define TYPE_MOS6522_CUDA "mos6522-cuda"
#define MOS6522_CUDA(obj) OBJECT_CHECK(MOS6522CUDAState, (obj), \
                                       TYPE_MOS6522_CUDA)

/* MacIO */
#define TYPE_OLDWORLD_MACIO "macio-oldworld"
#define TYPE_NEWWORLD_MACIO "macio-newworld"

#define TYPE_MACIO_IDE "macio-ide"
#define MACIO_IDE(obj) OBJECT_CHECK(MACIOIDEState, (obj), TYPE_MACIO_IDE)

typedef struct MACIOIDEState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t channel;
    qemu_irq real_ide_irq;
    qemu_irq real_dma_irq;
    qemu_irq ide_irq;
    qemu_irq dma_irq;

    MemoryRegion mem;
    IDEBus bus;
    IDEDMA dma;
    void *dbdma;
    bool dma_active;
    uint32_t timing_reg;
    uint32_t irq_reg;
} MACIOIDEState;

void macio_ide_init_drives(MACIOIDEState *ide, DriveInfo **hd_table);
void macio_ide_register_dma(MACIOIDEState *ide);

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
#endif /* PPC_MAC_H */
