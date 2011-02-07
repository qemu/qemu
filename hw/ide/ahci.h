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
 * version 2 of the License, or (at your option) any later version.
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

#ifndef HW_IDE_AHCI_H
#define HW_IDE_AHCI_H

#define AHCI_PCI_BAR              5
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

#define AHCI_IRQ_ON_SG            (1 << 31)
#define AHCI_CMD_ATAPI            (1 << 5)
#define AHCI_CMD_WRITE            (1 << 6)
#define AHCI_CMD_PREFETCH         (1 << 7)
#define AHCI_CMD_RESET            (1 << 8)
#define AHCI_CMD_CLR_BUSY         (1 << 10)

#define RX_FIS_D2H_REG            0x40 /* offset of D2H Register FIS data */
#define RX_FIS_SDB                0x58 /* offset of SDB FIS data */
#define RX_FIS_UNK                0x60 /* offset of Unknown FIS data */

/* global controller registers */
#define HOST_CAP                  0x00 /* host capabilities */
#define HOST_CTL                  0x04 /* global host control */
#define HOST_IRQ_STAT             0x08 /* interrupt status */
#define HOST_PORTS_IMPL           0x0c /* bitmap of implemented ports */
#define HOST_VERSION              0x10 /* AHCI spec. version compliancy */

/* HOST_CTL bits */
#define HOST_CTL_RESET            (1 << 0)  /* reset controller; self-clear */
#define HOST_CTL_IRQ_EN           (1 << 1)  /* global IRQ enable */
#define HOST_CTL_AHCI_EN          (1 << 31) /* AHCI enabled */

/* HOST_CAP bits */
#define HOST_CAP_SSC              (1 << 14) /* Slumber capable */
#define HOST_CAP_AHCI             (1 << 18) /* AHCI only */
#define HOST_CAP_CLO              (1 << 24) /* Command List Override support */
#define HOST_CAP_SSS              (1 << 27) /* Staggered Spin-up */
#define HOST_CAP_NCQ              (1 << 30) /* Native Command Queueing */
#define HOST_CAP_64               (1 << 31) /* PCI DAC (64-bit DMA) support */

/* registers for each SATA port */
#define PORT_LST_ADDR             0x00 /* command list DMA addr */
#define PORT_LST_ADDR_HI          0x04 /* command list DMA addr hi */
#define PORT_FIS_ADDR             0x08 /* FIS rx buf addr */
#define PORT_FIS_ADDR_HI          0x0c /* FIS rx buf addr hi */
#define PORT_IRQ_STAT             0x10 /* interrupt status */
#define PORT_IRQ_MASK             0x14 /* interrupt enable/disable mask */
#define PORT_CMD                  0x18 /* port command */
#define PORT_TFDATA               0x20 /* taskfile data */
#define PORT_SIG                  0x24 /* device TF signature */
#define PORT_SCR_STAT             0x28 /* SATA phy register: SStatus */
#define PORT_SCR_CTL              0x2c /* SATA phy register: SControl */
#define PORT_SCR_ERR              0x30 /* SATA phy register: SError */
#define PORT_SCR_ACT              0x34 /* SATA phy register: SActive */
#define PORT_CMD_ISSUE            0x38 /* command issue */
#define PORT_RESERVED             0x3c /* reserved */

/* PORT_IRQ_{STAT,MASK} bits */
#define PORT_IRQ_COLD_PRES        (1 << 31) /* cold presence detect */
#define PORT_IRQ_TF_ERR           (1 << 30) /* task file error */
#define PORT_IRQ_HBUS_ERR         (1 << 29) /* host bus fatal error */
#define PORT_IRQ_HBUS_DATA_ERR    (1 << 28) /* host bus data error */
#define PORT_IRQ_IF_ERR           (1 << 27) /* interface fatal error */
#define PORT_IRQ_IF_NONFATAL      (1 << 26) /* interface non-fatal error */
#define PORT_IRQ_OVERFLOW         (1 << 24) /* xfer exhausted available S/G */
#define PORT_IRQ_BAD_PMP          (1 << 23) /* incorrect port multiplier */

#define PORT_IRQ_PHYRDY           (1 << 22) /* PhyRdy changed */
#define PORT_IRQ_DEV_ILCK         (1 << 7) /* device interlock */
#define PORT_IRQ_CONNECT          (1 << 6) /* port connect change status */
#define PORT_IRQ_SG_DONE          (1 << 5) /* descriptor processed */
#define PORT_IRQ_UNK_FIS          (1 << 4) /* unknown FIS rx'd */
#define PORT_IRQ_SDB_FIS          (1 << 3) /* Set Device Bits FIS rx'd */
#define PORT_IRQ_DMAS_FIS         (1 << 2) /* DMA Setup FIS rx'd */
#define PORT_IRQ_PIOS_FIS         (1 << 1) /* PIO Setup FIS rx'd */
#define PORT_IRQ_D2H_REG_FIS      (1 << 0) /* D2H Register FIS rx'd */

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

#define PORT_CMD_ICC_MASK         (0xf << 28) /* i/f ICC state mask */
#define PORT_CMD_ICC_ACTIVE       (0x1 << 28) /* Put i/f in active state */
#define PORT_CMD_ICC_PARTIAL      (0x2 << 28) /* Put i/f in partial state */
#define PORT_CMD_ICC_SLUMBER      (0x6 << 28) /* Put i/f in slumber state */

#define PORT_IRQ_STAT_DHRS        (1 << 0) /* Device to Host Register FIS */
#define PORT_IRQ_STAT_PSS         (1 << 1) /* PIO Setup FIS */
#define PORT_IRQ_STAT_DSS         (1 << 2) /* DMA Setup FIS */
#define PORT_IRQ_STAT_SDBS        (1 << 3) /* Set Device Bits */
#define PORT_IRQ_STAT_UFS         (1 << 4) /* Unknown FIS */
#define PORT_IRQ_STAT_DPS         (1 << 5) /* Descriptor Processed */
#define PORT_IRQ_STAT_PCS         (1 << 6) /* Port Connect Change Status */
#define PORT_IRQ_STAT_DMPS        (1 << 7) /* Device Mechanical Presence
                                              Status */
#define PORT_IRQ_STAT_PRCS        (1 << 22) /* File Ready Status */
#define PORT_IRQ_STAT_IPMS        (1 << 23) /* Incorrect Port Multiplier
                                               Status */
#define PORT_IRQ_STAT_OFS         (1 << 24) /* Overflow Status */
#define PORT_IRQ_STAT_INFS        (1 << 26) /* Interface Non-Fatal Error
                                               Status */
#define PORT_IRQ_STAT_IFS         (1 << 27) /* Interface Fatal Error */
#define PORT_IRQ_STAT_HBDS        (1 << 28) /* Host Bus Data Error Status */
#define PORT_IRQ_STAT_HBFS        (1 << 29) /* Host Bus Fatal Error Status */
#define PORT_IRQ_STAT_TFES        (1 << 30) /* Task File Error Status */
#define PORT_IRQ_STAT_CPDS        (1 << 31) /* Code Port Detect Status */

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
#define SATA_FIS_REG_H2D_UPDATE_COMMAND_REGISTER 0x80

#define AHCI_CMD_HDR_CMD_FIS_LEN           0x1f
#define AHCI_CMD_HDR_PRDT_LEN              16

#define SATA_SIGNATURE_CDROM               0xeb140000
#define SATA_SIGNATURE_DISK                0x00000101

#define AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR 0x20
                                            /* Shouldn't this be 0x2c? */

#define AHCI_PORT_REGS_START_ADDR          0x100
#define AHCI_PORT_ADDR_OFFSET_MASK         0x7f
#define AHCI_PORT_ADDR_OFFSET_LEN          0x80

#define AHCI_NUM_COMMAND_SLOTS             31
#define AHCI_SUPPORTED_SPEED               20
#define AHCI_SUPPORTED_SPEED_GEN1          1
#define AHCI_VERSION_1_0                   0x10000

#define AHCI_PROGMODE_MAJOR_REV_1          1

#define AHCI_COMMAND_TABLE_ACMD            0x40

#define IDE_FEATURE_DMA                    1

#define READ_FPDMA_QUEUED                  0x60
#define WRITE_FPDMA_QUEUED                 0x61

#define RES_FIS_DSFIS                      0x00
#define RES_FIS_PSFIS                      0x20
#define RES_FIS_RFIS                       0x40
#define RES_FIS_SDBFIS                     0x58
#define RES_FIS_UFIS                       0x60

typedef struct AHCIControlRegs {
    uint32_t    cap;
    uint32_t    ghc;
    uint32_t    irqstatus;
    uint32_t    impl;
    uint32_t    version;
} AHCIControlRegs;

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
    uint32_t    opts;
    uint32_t    status;
    uint64_t    tbl_addr;
    uint32_t    reserved[4];
} __attribute__ ((packed)) AHCICmdHdr;

typedef struct AHCI_SG {
    uint64_t    addr;
    uint32_t    reserved;
    uint32_t    flags_size;
} __attribute__ ((packed)) AHCI_SG;

typedef struct AHCIDevice AHCIDevice;

typedef struct NCQTransferState {
    AHCIDevice *drive;
    BlockDriverAIOCB *aiocb;
    QEMUSGList sglist;
    int is_read;
    uint16_t sector_count;
    uint64_t lba;
    uint8_t tag;
    int slot;
    int used;
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
    int dma_status;
    int done_atapi_packet;
    int busy_slot;
    int init_d2h_sent;
    BlockDriverCompletionFunc *dma_cb;
    AHCICmdHdr *cur_cmd;
    NCQTransferState ncq_tfs[AHCI_MAX_CMDS];
};

typedef struct AHCIState {
    AHCIDevice *dev;
    AHCIControlRegs control_regs;
    int mem;
    int ports;
    qemu_irq irq;
} AHCIState;

typedef struct AHCIPCIState {
    PCIDevice card;
    AHCIState ahci;
} AHCIPCIState;

typedef struct NCQFrame {
    uint8_t fis_type;
    uint8_t c;
    uint8_t command;
    uint8_t sector_count_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t fua;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t sector_count_high;
    uint8_t tag;
    uint8_t reserved5;
    uint8_t reserved6;
    uint8_t control;
    uint8_t reserved7;
    uint8_t reserved8;
    uint8_t reserved9;
    uint8_t reserved10;
} __attribute__ ((packed)) NCQFrame;

void ahci_init(AHCIState *s, DeviceState *qdev, int ports);
void ahci_uninit(AHCIState *s);

void ahci_pci_map(PCIDevice *pci_dev, int region_num,
        pcibus_t addr, pcibus_t size, int type);

void ahci_reset(void *opaque);

#endif /* HW_IDE_AHCI_H */
