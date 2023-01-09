/*
 * QEMU AHCI Emulation
 *
 * Copyright (c) 2010 qiaochong@loongson.cn
 * Copyright (c) 2010 Roland Elek <elek.roland@gmail.com>
 * Copyright (c) 2010 Sebastian Herbszt <herbszt@gmx.de>
 * Copyright (c) 2010 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HW_IDE_AHCI_INTERNAL_H
#define HW_IDE_AHCI_INTERNAL_H

#include "hw/ide/ahci.h"
#include "hw/ide/internal.h"
#include "hw/pci/pci_device.h"

#define AHCI_MEM_BAR_SIZE         0x1000
#define AHCI_MAX_PORTS            32
#define AHCI_MAX_SG               168 /* hardware max is 64K */
#define AHCI_DMA_BOUNDARY         0xffffffff
#define AHCI_USE_CLUSTERING       0
#define AHCI_MAX_CMDS             32
#define AHCI_CMD_SZ               32
#define AHCI_CMD_SLOT_SZ          (AHCI_MAX_CMDS * AHCI_CMD_SZ)
#define AHCI_RX_FIS_SZ            256
#define AHCI_CMD_TBL_CDB          0x40
#define AHCI_CMD_TBL_HDR_SZ       0x80
#define AHCI_CMD_TBL_SZ           (AHCI_CMD_TBL_HDR_SZ + (AHCI_MAX_SG * 16))
#define AHCI_CMD_TBL_AR_SZ        (AHCI_CMD_TBL_SZ * AHCI_MAX_CMDS)
#define AHCI_PORT_PRIV_DMA_SZ     (AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_AR_SZ + \
                                   AHCI_RX_FIS_SZ)

#define AHCI_IRQ_ON_SG            (1U << 31)
#define AHCI_CMD_ATAPI            (1 << 5)
#define AHCI_CMD_WRITE            (1 << 6)
#define AHCI_CMD_PREFETCH         (1 << 7)
#define AHCI_CMD_RESET            (1 << 8)
#define AHCI_CMD_CLR_BUSY         (1 << 10)

#define RX_FIS_D2H_REG            0x40 /* offset of D2H Register FIS data */
#define RX_FIS_SDB                0x58 /* offset of SDB FIS data */
#define RX_FIS_UNK                0x60 /* offset of Unknown FIS data */

/* global controller registers */
enum AHCIHostReg {
    AHCI_HOST_REG_CAP        = 0,  /* CAP: host capabilities */
    AHCI_HOST_REG_CTL        = 1,  /* GHC: global host control */
    AHCI_HOST_REG_IRQ_STAT   = 2,  /* IS: interrupt status */
    AHCI_HOST_REG_PORTS_IMPL = 3,  /* PI: bitmap of implemented ports */
    AHCI_HOST_REG_VERSION    = 4,  /* VS: AHCI spec. version compliancy */
    AHCI_HOST_REG_CCC_CTL    = 5,  /* CCC_CTL: CCC Control */
    AHCI_HOST_REG_CCC_PORTS  = 6,  /* CCC_PORTS: CCC Ports */
    AHCI_HOST_REG_EM_LOC     = 7,  /* EM_LOC: Enclosure Mgmt Location */
    AHCI_HOST_REG_EM_CTL     = 8,  /* EM_CTL: Enclosure Mgmt Control */
    AHCI_HOST_REG_CAP2       = 9,  /* CAP2: host capabilities, extended */
    AHCI_HOST_REG_BOHC       = 10, /* BOHC: firmare/os handoff ctrl & status */
    AHCI_HOST_REG__COUNT     = 11
};

/* HOST_CTL bits */
#define HOST_CTL_RESET            (1 << 0)  /* reset controller; self-clear */
#define HOST_CTL_IRQ_EN           (1 << 1)  /* global IRQ enable */
#define HOST_CTL_AHCI_EN          (1U << 31) /* AHCI enabled */

/* HOST_CAP bits */
#define HOST_CAP_SSC              (1 << 14) /* Slumber capable */
#define HOST_CAP_AHCI             (1 << 18) /* AHCI only */
#define HOST_CAP_CLO              (1 << 24) /* Command List Override support */
#define HOST_CAP_SSS              (1 << 27) /* Staggered Spin-up */
#define HOST_CAP_NCQ              (1 << 30) /* Native Command Queueing */
#define HOST_CAP_64               (1U << 31) /* PCI DAC (64-bit DMA) support */

/* registers for each SATA port */
enum AHCIPortReg {
    AHCI_PORT_REG_LST_ADDR    = 0, /* PxCLB: command list DMA addr */
    AHCI_PORT_REG_LST_ADDR_HI = 1, /* PxCLBU: command list DMA addr hi */
    AHCI_PORT_REG_FIS_ADDR    = 2, /* PxFB: FIS rx buf addr */
    AHCI_PORT_REG_FIS_ADDR_HI = 3, /* PxFBU: FIX rx buf addr hi */
    AHCI_PORT_REG_IRQ_STAT    = 4, /* PxIS: interrupt status */
    AHCI_PORT_REG_IRQ_MASK    = 5, /* PxIE: interrupt enable/disable mask */
    AHCI_PORT_REG_CMD         = 6, /* PxCMD: port command */
    /* RESERVED */
    AHCI_PORT_REG_TFDATA      = 8, /* PxTFD: taskfile data */
    AHCI_PORT_REG_SIG         = 9, /* PxSIG: device TF signature */
    AHCI_PORT_REG_SCR_STAT    = 10, /* PxSSTS: SATA phy register: SStatus */
    AHCI_PORT_REG_SCR_CTL     = 11, /* PxSCTL: SATA phy register: SControl */
    AHCI_PORT_REG_SCR_ERR     = 12, /* PxSERR: SATA phy register: SError */
    AHCI_PORT_REG_SCR_ACT     = 13, /* PxSACT: SATA phy register: SActive */
    AHCI_PORT_REG_CMD_ISSUE   = 14, /* PxCI: command issue */
    AHCI_PORT_REG_SCR_NOTIF   = 15, /* PxSNTF: SATA phy register: SNotification */
    AHCI_PORT_REG_FIS_CTL     = 16, /* PxFBS: Port multiplier switching ctl */
    AHCI_PORT_REG_DEV_SLEEP   = 17, /* PxDEVSLP: device sleep control */
    /* RESERVED */
    AHCI_PORT_REG_VENDOR_1    = 28, /* PxVS: Vendor Specific */
    AHCI_PORT_REG_VENDOR_2    = 29,
    AHCI_PORT_REG_VENDOR_3    = 30,
    AHCI_PORT_REG_VENDOR_4    = 31,
    AHCI_PORT_REG__COUNT      = 32
};

/* Port interrupt bit descriptors */
enum AHCIPortIRQ {
    AHCI_PORT_IRQ_BIT_DHRS = 0,
    AHCI_PORT_IRQ_BIT_PSS  = 1,
    AHCI_PORT_IRQ_BIT_DSS  = 2,
    AHCI_PORT_IRQ_BIT_SDBS = 3,
    AHCI_PORT_IRQ_BIT_UFS  = 4,
    AHCI_PORT_IRQ_BIT_DPS  = 5,
    AHCI_PORT_IRQ_BIT_PCS  = 6,
    AHCI_PORT_IRQ_BIT_DMPS = 7,
    /* RESERVED */
    AHCI_PORT_IRQ_BIT_PRCS = 22,
    AHCI_PORT_IRQ_BIT_IPMS = 23,
    AHCI_PORT_IRQ_BIT_OFS  = 24,
    /* RESERVED */
    AHCI_PORT_IRQ_BIT_INFS = 26,
    AHCI_PORT_IRQ_BIT_IFS  = 27,
    AHCI_PORT_IRQ_BIT_HBDS = 28,
    AHCI_PORT_IRQ_BIT_HBFS = 29,
    AHCI_PORT_IRQ_BIT_TFES = 30,
    AHCI_PORT_IRQ_BIT_CPDS = 31,
    AHCI_PORT_IRQ__COUNT   = 32
};


/* PORT_IRQ_{STAT,MASK} bits */
#define PORT_IRQ_COLD_PRES        (1U << 31) /* cold presence detect */
#define PORT_IRQ_TF_ERR           (1 << 30) /* task file error */
#define PORT_IRQ_HBUS_ERR         (1 << 29) /* host bus fatal error */
#define PORT_IRQ_HBUS_DATA_ERR    (1 << 28) /* host bus data error */
#define PORT_IRQ_IF_ERR           (1 << 27) /* interface fatal error */
#define PORT_IRQ_IF_NONFATAL      (1 << 26) /* interface non-fatal error */
                                            /* reserved */
#define PORT_IRQ_OVERFLOW         (1 << 24) /* xfer exhausted available S/G */
#define PORT_IRQ_BAD_PMP          (1 << 23) /* incorrect port multiplier */
#define PORT_IRQ_PHYRDY           (1 << 22) /* PhyRdy changed */
                                            /* reserved */
#define PORT_IRQ_DEV_ILCK         (1 << 7)  /* device interlock */
#define PORT_IRQ_CONNECT          (1 << 6)  /* port connect change status */
#define PORT_IRQ_SG_DONE          (1 << 5)  /* descriptor processed */
#define PORT_IRQ_UNK_FIS          (1 << 4)  /* unknown FIS rx'd */
#define PORT_IRQ_SDB_FIS          (1 << 3)  /* Set Device Bits FIS rx'd */
#define PORT_IRQ_DMAS_FIS         (1 << 2)  /* DMA Setup FIS rx'd */
#define PORT_IRQ_PIOS_FIS         (1 << 1)  /* PIO Setup FIS rx'd */
#define PORT_IRQ_D2H_REG_FIS      (1 << 0)  /* D2H Register FIS rx'd */

#define PORT_IRQ_FREEZE           (PORT_IRQ_HBUS_ERR | PORT_IRQ_IF_ERR |   \
                                   PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY |    \
                                   PORT_IRQ_UNK_FIS)
#define PORT_IRQ_ERROR            (PORT_IRQ_FREEZE | PORT_IRQ_TF_ERR |     \
                                   PORT_IRQ_HBUS_DATA_ERR)
#define DEF_PORT_IRQ              (PORT_IRQ_ERROR | PORT_IRQ_SG_DONE |     \
                                   PORT_IRQ_SDB_FIS | PORT_IRQ_DMAS_FIS |  \
                                   PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS)

/* PORT_CMD bits */
#define PORT_CMD_ATAPI            (1 << 24) /* Device is ATAPI */
#define PORT_CMD_LIST_ON          (1 << 15) /* cmd list DMA engine running */
#define PORT_CMD_FIS_ON           (1 << 14) /* FIS DMA engine running */
#define PORT_CMD_FIS_RX           (1 << 4) /* Enable FIS receive DMA engine */
#define PORT_CMD_CLO              (1 << 3) /* Command list override */
#define PORT_CMD_POWER_ON         (1 << 2) /* Power up device */
#define PORT_CMD_SPIN_UP          (1 << 1) /* Spin up device */
#define PORT_CMD_START            (1 << 0) /* Enable port DMA engine */

#define PORT_CMD_ICC_MASK        (0xfU << 28) /* i/f ICC state mask */
#define PORT_CMD_ICC_ACTIVE       (0x1 << 28) /* Put i/f in active state */
#define PORT_CMD_ICC_PARTIAL      (0x2 << 28) /* Put i/f in partial state */
#define PORT_CMD_ICC_SLUMBER      (0x6 << 28) /* Put i/f in slumber state */

#define PORT_CMD_RO_MASK          0x007dffe0 /* Which CMD bits are read only? */

/* ap->flags bits */
#define AHCI_FLAG_NO_NCQ                  (1 << 24)
#define AHCI_FLAG_IGN_IRQ_IF_ERR          (1 << 25) /* ignore IRQ_IF_ERR */
#define AHCI_FLAG_HONOR_PI                (1 << 26) /* honor PORTS_IMPL */
#define AHCI_FLAG_IGN_SERR_INTERNAL       (1 << 27) /* ignore SERR_INTERNAL */
#define AHCI_FLAG_32BIT_ONLY              (1 << 28) /* force 32bit */

#define ATA_SRST                          (1 << 2)  /* software reset */

#define STATE_RUN                         0
#define STATE_RESET                       1

#define SATA_SCR_SSTATUS_DET_NODEV        0x0
#define SATA_SCR_SSTATUS_DET_DEV_PRESENT_PHY_UP 0x3

#define SATA_SCR_SSTATUS_SPD_NODEV        0x00
#define SATA_SCR_SSTATUS_SPD_GEN1         0x10

#define SATA_SCR_SSTATUS_IPM_NODEV        0x000
#define SATA_SCR_SSTATUS_IPM_ACTIVE       0X100

#define AHCI_SCR_SCTL_DET                 0xf

#define SATA_FIS_TYPE_REGISTER_H2D        0x27
#define   SATA_FIS_REG_H2D_UPDATE_COMMAND_REGISTER 0x80
#define SATA_FIS_TYPE_REGISTER_D2H        0x34
#define SATA_FIS_TYPE_PIO_SETUP           0x5f
#define SATA_FIS_TYPE_SDB                 0xA1

#define AHCI_CMD_HDR_CMD_FIS_LEN           0x1f
#define AHCI_CMD_HDR_PRDT_LEN              16

#define SATA_SIGNATURE_CDROM               0xeb140101
#define SATA_SIGNATURE_DISK                0x00000101

#define AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR 0x2c

#define AHCI_PORT_REGS_START_ADDR          0x100
#define AHCI_PORT_ADDR_OFFSET_MASK         0x7f
#define AHCI_PORT_ADDR_OFFSET_LEN          0x80

#define AHCI_NUM_COMMAND_SLOTS             31
#define AHCI_SUPPORTED_SPEED               20
#define AHCI_SUPPORTED_SPEED_GEN1          1
#define AHCI_VERSION_1_0                   0x10000

#define AHCI_PROGMODE_MAJOR_REV_1          1

#define AHCI_COMMAND_TABLE_ACMD            0x40

#define AHCI_PRDT_SIZE_MASK                0x3fffff

#define IDE_FEATURE_DMA                    1

#define READ_FPDMA_QUEUED                  0x60
#define WRITE_FPDMA_QUEUED                 0x61
#define NCQ_NON_DATA                       0x63
#define RECEIVE_FPDMA_QUEUED               0x65
#define SEND_FPDMA_QUEUED                  0x64

#define NCQ_FIS_FUA_MASK                   0x80
#define NCQ_FIS_RARC_MASK                  0x01

#define RES_FIS_DSFIS                      0x00
#define RES_FIS_PSFIS                      0x20
#define RES_FIS_RFIS                       0x40
#define RES_FIS_SDBFIS                     0x58
#define RES_FIS_UFIS                       0x60

#define SATA_CAP_SIZE           0x8
#define SATA_CAP_REV            0x2
#define SATA_CAP_BAR            0x4

typedef struct AHCIPortRegs {
    uint32_t    lst_addr;
    uint32_t    lst_addr_hi;
    uint32_t    fis_addr;
    uint32_t    fis_addr_hi;
    uint32_t    irq_stat;
    uint32_t    irq_mask;
    uint32_t    cmd;
    uint32_t    unused0;
    uint32_t    tfdata;
    uint32_t    sig;
    uint32_t    scr_stat;
    uint32_t    scr_ctl;
    uint32_t    scr_err;
    uint32_t    scr_act;
    uint32_t    cmd_issue;
    uint32_t    reserved;
} AHCIPortRegs;

typedef struct AHCICmdHdr {
    uint16_t    opts;
    uint16_t    prdtl;
    uint32_t    status;
    uint64_t    tbl_addr;
    uint32_t    reserved[4];
} QEMU_PACKED AHCICmdHdr;

typedef struct AHCI_SG {
    uint64_t    addr;
    uint32_t    reserved;
    uint32_t    flags_size;
} QEMU_PACKED AHCI_SG;

typedef struct NCQTransferState {
    AHCIDevice *drive;
    BlockAIOCB *aiocb;
    AHCICmdHdr *cmdh;
    QEMUSGList sglist;
    BlockAcctCookie acct;
    uint32_t sector_count;
    uint64_t lba;
    uint8_t tag;
    uint8_t cmd;
    uint8_t slot;
    bool used;
    bool halt;
} NCQTransferState;

struct AHCIDevice {
    IDEDMA dma;
    IDEBus port;
    int port_no;
    uint32_t port_state;
    uint32_t finished;
    AHCIPortRegs port_regs;
    struct AHCIState *hba;
    QEMUBH *check_bh;
    uint8_t *lst;
    uint8_t *res_fis;
    bool done_first_drq;
    int32_t busy_slot;
    bool init_d2h_sent;
    AHCICmdHdr *cur_cmd;
    NCQTransferState ncq_tfs[AHCI_MAX_CMDS];
};

struct AHCIPCIState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    AHCIState ahci;
};

extern const VMStateDescription vmstate_ahci;

#define VMSTATE_AHCI(_field, _state) {                               \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(AHCIState),                                 \
    .vmsd       = &vmstate_ahci,                                     \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, AHCIState),   \
}

/**
 * NCQFrame is the same as a Register H2D FIS (described in SATA 3.2),
 * but some fields have been re-mapped and re-purposed, as seen in
 * SATA 3.2 section 13.6.4.1 ("READ FPDMA QUEUED")
 *
 * cmd_fis[3], feature 7:0, becomes sector count 7:0.
 * cmd_fis[7], device 7:0, uses bit 7 as the Force Unit Access bit.
 * cmd_fis[11], feature 15:8, becomes sector count 15:8.
 * cmd_fis[12], count 7:0, becomes the NCQ TAG (7:3) and RARC bit (0)
 * cmd_fis[13], count 15:8, becomes the priority value (7:6)
 * bytes 16-19 become an le32 "auxiliary" field.
 */
typedef struct NCQFrame {
    uint8_t fis_type;
    uint8_t c;
    uint8_t command;
    uint8_t sector_count_low;  /* (feature 7:0) */
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t fua;               /* (device 7:0) */
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t sector_count_high; /* (feature 15:8) */
    uint8_t tag;               /* (count 0:7) */
    uint8_t prio;              /* (count 15:8) */
    uint8_t icc;
    uint8_t control;
    uint8_t aux0;
    uint8_t aux1;
    uint8_t aux2;
    uint8_t aux3;
} QEMU_PACKED NCQFrame;

typedef struct SDBFIS {
    uint8_t type;
    uint8_t flags;
    uint8_t status;
    uint8_t error;
    uint32_t payload;
} QEMU_PACKED SDBFIS;

void ahci_realize(AHCIState *s, DeviceState *qdev, AddressSpace *as, int ports);
void ahci_init(AHCIState *s, DeviceState *qdev);
void ahci_uninit(AHCIState *s);

void ahci_reset(AHCIState *s);

#endif /* HW_IDE_AHCI_INTERNAL_H */
