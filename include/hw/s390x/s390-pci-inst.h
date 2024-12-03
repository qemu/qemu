/*
 * s390 PCI instruction definitions
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Frank Blaschka <frank.blaschka@de.ibm.com>
 *            Hong Bo Li <lihbbj@cn.ibm.com>
 *            Yi Min Zhao <zyimin@cn.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_PCI_INST_H
#define HW_S390_PCI_INST_H

#include "s390-pci-bus.h"
#include "system/dma.h"

/* Load/Store status codes */
#define ZPCI_PCI_ST_FUNC_NOT_ENABLED        4
#define ZPCI_PCI_ST_FUNC_IN_ERR             8
#define ZPCI_PCI_ST_BLOCKED                 12
#define ZPCI_PCI_ST_INSUF_RES               16
#define ZPCI_PCI_ST_INVAL_AS                20
#define ZPCI_PCI_ST_FUNC_ALREADY_ENABLED    24
#define ZPCI_PCI_ST_DMA_AS_NOT_ENABLED      28
#define ZPCI_PCI_ST_2ND_OP_IN_INV_AS        36
#define ZPCI_PCI_ST_FUNC_NOT_AVAIL          40
#define ZPCI_PCI_ST_ALREADY_IN_RQ_STATE     44

/* Load/Store return codes */
#define ZPCI_PCI_LS_OK              0
#define ZPCI_PCI_LS_ERR             1
#define ZPCI_PCI_LS_BUSY            2
#define ZPCI_PCI_LS_INVAL_HANDLE    3

/* Modify PCI status codes */
#define ZPCI_MOD_ST_RES_NOT_AVAIL 4
#define ZPCI_MOD_ST_INSUF_RES     16
#define ZPCI_MOD_ST_SEQUENCE      24
#define ZPCI_MOD_ST_DMAAS_INVAL   28
#define ZPCI_MOD_ST_FRAME_INVAL   32
#define ZPCI_MOD_ST_ERROR_RECOVER 40

/* Modify PCI Function Controls */
#define ZPCI_MOD_FC_REG_INT     2
#define ZPCI_MOD_FC_DEREG_INT   3
#define ZPCI_MOD_FC_REG_IOAT    4
#define ZPCI_MOD_FC_DEREG_IOAT  5
#define ZPCI_MOD_FC_REREG_IOAT  6
#define ZPCI_MOD_FC_RESET_ERROR 7
#define ZPCI_MOD_FC_RESET_BLOCK 9
#define ZPCI_MOD_FC_SET_MEASURE 10

/* Store PCI Function Controls status codes */
#define ZPCI_STPCIFC_ST_PERM_ERROR    8
#define ZPCI_STPCIFC_ST_INVAL_DMAAS   28
#define ZPCI_STPCIFC_ST_ERROR_RECOVER 40

/* Refresh PCI Translations status codes */
#define ZPCI_RPCIT_ST_INSUFF_RES      16

/* FIB function controls */
#define ZPCI_FIB_FC_ENABLED     0x80
#define ZPCI_FIB_FC_ERROR       0x40
#define ZPCI_FIB_FC_LS_BLOCKED  0x20
#define ZPCI_FIB_FC_DMAAS_REG   0x10

/* FIB function controls */
#define ZPCI_FIB_FC_ENABLED     0x80
#define ZPCI_FIB_FC_ERROR       0x40
#define ZPCI_FIB_FC_LS_BLOCKED  0x20
#define ZPCI_FIB_FC_DMAAS_REG   0x10

/* Function Information Block */
typedef struct ZpciFib {
    uint8_t fmt;   /* format */
    uint8_t reserved1[7];
    uint8_t fc;                  /* function controls */
    uint8_t reserved2;
    uint16_t reserved3;
    uint32_t reserved4;
    uint64_t pba;                /* PCI base address */
    uint64_t pal;                /* PCI address limit */
    uint64_t iota;               /* I/O Translation Anchor */
#define FIB_DATA_ISC(x)    (((x) >> 28) & 0x7)
#define FIB_DATA_NOI(x)    (((x) >> 16) & 0xfff)
#define FIB_DATA_AIBVO(x) (((x) >> 8) & 0x3f)
#define FIB_DATA_SUM(x)    (((x) >> 7) & 0x1)
#define FIB_DATA_AISBO(x)  ((x) & 0x3f)
    uint32_t data;
    uint32_t reserved5;
    uint64_t aibv;               /* Adapter int bit vector address */
    uint64_t aisb;               /* Adapter int summary bit address */
    uint64_t fmb_addr;           /* Function measurement address and key */
    uint32_t reserved6;
    uint32_t gd;
} QEMU_PACKED ZpciFib;

int pci_dereg_irqs(S390PCIBusDevice *pbdev);
void pci_dereg_ioat(S390PCIIOMMU *iommu);
int clp_service_call(S390CPU *cpu, uint8_t r2, uintptr_t ra);
int pcilg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2, uintptr_t ra);
int pcistg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2, uintptr_t ra);
int rpcit_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2, uintptr_t ra);
int pcistb_service_call(S390CPU *cpu, uint8_t r1, uint8_t r3, uint64_t gaddr,
                        uint8_t ar, uintptr_t ra);
int mpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar,
                        uintptr_t ra);
int stpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar,
                         uintptr_t ra);
void fmb_timer_free(S390PCIBusDevice *pbdev);

#define ZPCI_IO_BAR_MIN 0
#define ZPCI_IO_BAR_MAX 5
#define ZPCI_CONFIG_BAR 15

#endif
