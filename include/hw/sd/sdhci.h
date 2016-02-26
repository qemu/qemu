/*
 * SD Association Host Standard Specification v2.0 controller emulation
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Mitsyanko Igor <i.mitsyanko@samsung.com>
 * Peter A.G. Crosthwaite <peter.crosthwaite@petalogix.com>
 *
 * Based on MMC controller for Samsung S5PC1xx-based board emulation
 * by Alexey Merkulov and Vladimir Monakhov.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU _General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SDHCI_H
#define SDHCI_H

#include "qemu-common.h"
#include "hw/block/block.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"

/* SD/MMC host controller state */
typedef struct SDHCIState {
    union {
        PCIDevice pcidev;
        SysBusDevice busdev;
    };
    SDBus sdbus;
    MemoryRegion iomem;

    QEMUTimer *insert_timer;       /* timer for 'changing' sd card. */
    QEMUTimer *transfer_timer;
    qemu_irq eject_cb;
    qemu_irq ro_cb;
    qemu_irq irq;

    uint32_t sdmasysad;    /* SDMA System Address register */
    uint16_t blksize;      /* Host DMA Buff Boundary and Transfer BlkSize Reg */
    uint16_t blkcnt;       /* Blocks count for current transfer */
    uint32_t argument;     /* Command Argument Register */
    uint16_t trnmod;       /* Transfer Mode Setting Register */
    uint16_t cmdreg;       /* Command Register */
    uint32_t rspreg[4];    /* Response Registers 0-3 */
    uint32_t prnsts;       /* Present State Register */
    uint8_t  hostctl;      /* Host Control Register */
    uint8_t  pwrcon;       /* Power control Register */
    uint8_t  blkgap;       /* Block Gap Control Register */
    uint8_t  wakcon;       /* WakeUp Control Register */
    uint16_t clkcon;       /* Clock control Register */
    uint8_t  timeoutcon;   /* Timeout Control Register */
    uint8_t  admaerr;      /* ADMA Error Status Register */
    uint16_t norintsts;    /* Normal Interrupt Status Register */
    uint16_t errintsts;    /* Error Interrupt Status Register */
    uint16_t norintstsen;  /* Normal Interrupt Status Enable Register */
    uint16_t errintstsen;  /* Error Interrupt Status Enable Register */
    uint16_t norintsigen;  /* Normal Interrupt Signal Enable Register */
    uint16_t errintsigen;  /* Error Interrupt Signal Enable Register */
    uint16_t acmd12errsts; /* Auto CMD12 error status register */
    uint64_t admasysaddr;  /* ADMA System Address Register */

    uint32_t capareg;      /* Capabilities Register */
    uint32_t maxcurr;      /* Maximum Current Capabilities Register */
    uint8_t  *fifo_buffer; /* SD host i/o FIFO buffer */
    uint32_t buf_maxsz;
    uint16_t data_count;   /* current element in FIFO buffer */
    uint8_t  stopped_state;/* Current SDHC state */
    bool     pending_insert_quirk;/* Quirk for Raspberry Pi card insert int */
    bool     pending_insert_state;
    /* Buffer Data Port Register - virtual access point to R and W buffers */
    /* Software Reset Register - always reads as 0 */
    /* Force Event Auto CMD12 Error Interrupt Reg - write only */
    /* Force Event Error Interrupt Register- write only */
    /* RO Host Controller Version Register always reads as 0x2401 */
} SDHCIState;

#define TYPE_PCI_SDHCI "sdhci-pci"
#define PCI_SDHCI(obj) OBJECT_CHECK(SDHCIState, (obj), TYPE_PCI_SDHCI)

#define TYPE_SYSBUS_SDHCI "generic-sdhci"
#define SYSBUS_SDHCI(obj)                               \
     OBJECT_CHECK(SDHCIState, (obj), TYPE_SYSBUS_SDHCI)

#endif /* SDHCI_H */
