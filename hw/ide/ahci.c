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
 *
 * lspci dump of a ICH-9 real device in IDE mode (hopefully close enough):
 *
 * 00:1f.2 SATA controller [0106]: Intel Corporation 82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA AHCI Controller [8086:2922] (rev 02) (prog-if 01 [AHCI 1.0])
 *         Subsystem: Intel Corporation 82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA AHCI Controller [8086:2922]
 *         Control: I/O+ Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
 *         Status: Cap+ 66MHz+ UDF- FastB2B+ ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
 *         Latency: 0
 *         Interrupt: pin B routed to IRQ 222
 *         Region 0: I/O ports at d000 [size=8]
 *         Region 1: I/O ports at cc00 [size=4]
 *         Region 2: I/O ports at c880 [size=8]
 *         Region 3: I/O ports at c800 [size=4]
 *         Region 4: I/O ports at c480 [size=32]
 *         Region 5: Memory at febf9000 (32-bit, non-prefetchable) [size=2K]
 *         Capabilities: [80] Message Signalled Interrupts: Mask- 64bit- Count=1/16 Enable+
 *                 Address: fee0f00c  Data: 41d9
 *         Capabilities: [70] Power Management version 3
 *                 Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0-,D1-,D2-,D3hot+,D3cold-)
 *                 Status: D0 PME-Enable- DSel=0 DScale=0 PME-
 *         Capabilities: [a8] SATA HBA <?>
 *         Capabilities: [b0] Vendor Specific Information <?>
 *         Kernel driver in use: ahci
 *         Kernel modules: ahci
 * 00: 86 80 22 29 07 04 b0 02 02 01 06 01 00 00 00 00
 * 10: 01 d0 00 00 01 cc 00 00 81 c8 00 00 01 c8 00 00
 * 20: 81 c4 00 00 00 90 bf fe 00 00 00 00 86 80 22 29
 * 30: 00 00 00 00 80 00 00 00 00 00 00 00 0f 02 00 00
 * 40: 00 80 00 80 00 00 00 00 00 00 00 00 00 00 00 00
 * 50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 01 a8 03 40 08 00 00 00 00 00 00 00 00 00 00 00
 * 80: 05 70 09 00 0c f0 e0 fe d9 41 00 00 00 00 00 00
 * 90: 40 00 0f 82 93 01 00 00 00 00 00 00 00 00 00 00
 * a0: ac 00 00 00 0a 00 12 00 12 b0 10 00 48 00 00 00
 * b0: 09 00 06 20 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 86 0f 02 00 00 00 00 00
 *
 */

#include <hw/hw.h>
#include <hw/msi.h>
#include <hw/pc.h>
#include <hw/pci.h>

#include "monitor.h"
#include "dma.h"
#include "cpu-common.h"
#include "blockdev.h"
#include "internal.h"
#include <hw/ide/pci.h>

/* #define DEBUG_AHCI */

#ifdef DEBUG_AHCI
#define DPRINTF(port, fmt, ...) \
do { fprintf(stderr, "ahci: %s: [%d] ", __FUNCTION__, port); \
     fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(port, fmt, ...) do {} while(0)
#endif

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

#define SATA_PORTS                         4

#define AHCI_PORT_REGS_START_ADDR          0x100
#define AHCI_PORT_REGS_END_ADDR (AHCI_PORT_REGS_START_ADDR + SATA_PORTS * 0x80)
#define AHCI_PORT_ADDR_OFFSET_MASK         0x7f

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
    BlockDriverCompletionFunc *dma_cb;
    AHCICmdHdr *cur_cmd;
    NCQTransferState ncq_tfs[AHCI_MAX_CMDS];
};

typedef struct AHCIState {
    AHCIDevice dev[SATA_PORTS];
    AHCIControlRegs control_regs;
    int mem;
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

static void check_cmd(AHCIState *s, int port);
static int handle_cmd(AHCIState *s,int port,int slot);
static void ahci_reset_port(AHCIState *s, int port);
static void ahci_write_fis_d2h(AHCIDevice *ad, uint8_t *cmd_fis);

static uint32_t  ahci_port_read(AHCIState *s, int port, int offset)
{
    uint32_t val;
    AHCIPortRegs *pr;
    pr = &s->dev[port].port_regs;

    switch (offset) {
    case PORT_LST_ADDR:
        val = pr->lst_addr;
        break;
    case PORT_LST_ADDR_HI:
        val = pr->lst_addr_hi;
        break;
    case PORT_FIS_ADDR:
        val = pr->fis_addr;
        break;
    case PORT_FIS_ADDR_HI:
        val = pr->fis_addr_hi;
        break;
    case PORT_IRQ_STAT:
        val = pr->irq_stat;
        break;
    case PORT_IRQ_MASK:
        val = pr->irq_mask;
        break;
    case PORT_CMD:
        val = pr->cmd;
        break;
    case PORT_TFDATA:
        val = ((uint16_t)s->dev[port].port.ifs[0].error << 8) |
              s->dev[port].port.ifs[0].status;
        break;
    case PORT_SIG:
        val = pr->sig;
        break;
    case PORT_SCR_STAT:
        if (s->dev[port].port.ifs[0].bs) {
            val = SATA_SCR_SSTATUS_DET_DEV_PRESENT_PHY_UP |
                  SATA_SCR_SSTATUS_SPD_GEN1 | SATA_SCR_SSTATUS_IPM_ACTIVE;
        } else {
            val = SATA_SCR_SSTATUS_DET_NODEV;
        }
        break;
    case PORT_SCR_CTL:
        val = pr->scr_ctl;
        break;
    case PORT_SCR_ERR:
        val = pr->scr_err;
        break;
    case PORT_SCR_ACT:
        pr->scr_act &= ~s->dev[port].finished;
        s->dev[port].finished = 0;
        val = pr->scr_act;
        break;
    case PORT_CMD_ISSUE:
        val = pr->cmd_issue;
        break;
    case PORT_RESERVED:
    default:
        val = 0;
    }
    DPRINTF(port, "offset: 0x%x val: 0x%x\n", offset, val);
    return val;

}

static void ahci_irq_raise(AHCIState *s, AHCIDevice *dev)
{
    struct AHCIPCIState *d = container_of(s, AHCIPCIState, ahci);

    DPRINTF(0, "raise irq\n");

    if (msi_enabled(&d->card)) {
        msi_notify(&d->card, 0);
    } else {
        qemu_irq_raise(s->irq);
    }
}

static void ahci_irq_lower(AHCIState *s, AHCIDevice *dev)
{
    struct AHCIPCIState *d = container_of(s, AHCIPCIState, ahci);

    DPRINTF(0, "lower irq\n");

    if (!msi_enabled(&d->card)) {
        qemu_irq_lower(s->irq);
    }
}

static void ahci_check_irq(AHCIState *s)
{
    int i;

    DPRINTF(-1, "check irq %#x\n", s->control_regs.irqstatus);

    for (i = 0; i < SATA_PORTS; i++) {
        AHCIPortRegs *pr = &s->dev[i].port_regs;
        if (pr->irq_stat & pr->irq_mask) {
            s->control_regs.irqstatus |= (1 << i);
        }
    }

    if (s->control_regs.irqstatus &&
        (s->control_regs.ghc & HOST_CTL_IRQ_EN)) {
            ahci_irq_raise(s, NULL);
    } else {
        ahci_irq_lower(s, NULL);
    }
}

static void ahci_trigger_irq(AHCIState *s, AHCIDevice *d,
                             int irq_type)
{
    DPRINTF(d->port_no, "trigger irq %#x -> %x\n",
            irq_type, d->port_regs.irq_mask & irq_type);

    d->port_regs.irq_stat |= irq_type;
    ahci_check_irq(s);
}

static void map_page(uint8_t **ptr, uint64_t addr, uint32_t wanted)
{
    target_phys_addr_t len = wanted;

    if (*ptr) {
        cpu_physical_memory_unmap(*ptr, 1, len, len);
    }

    *ptr = cpu_physical_memory_map(addr, &len, 1);
    if (len < wanted) {
        cpu_physical_memory_unmap(*ptr, 1, len, len);
        *ptr = NULL;
    }
}

static void  ahci_port_write(AHCIState *s, int port, int offset, uint32_t val)
{
    AHCIPortRegs *pr = &s->dev[port].port_regs;

    DPRINTF(port, "offset: 0x%x val: 0x%x\n", offset, val);
    switch (offset) {
        case PORT_LST_ADDR:
            pr->lst_addr = val;
            map_page(&s->dev[port].lst,
                     ((uint64_t)pr->lst_addr_hi << 32) | pr->lst_addr, 1024);
            s->dev[port].cur_cmd = NULL;
            break;
        case PORT_LST_ADDR_HI:
            pr->lst_addr_hi = val;
            map_page(&s->dev[port].lst,
                     ((uint64_t)pr->lst_addr_hi << 32) | pr->lst_addr, 1024);
            s->dev[port].cur_cmd = NULL;
            break;
        case PORT_FIS_ADDR:
            pr->fis_addr = val;
            map_page(&s->dev[port].res_fis,
                     ((uint64_t)pr->fis_addr_hi << 32) | pr->fis_addr, 256);
            break;
        case PORT_FIS_ADDR_HI:
            pr->fis_addr_hi = val;
            map_page(&s->dev[port].res_fis,
                     ((uint64_t)pr->fis_addr_hi << 32) | pr->fis_addr, 256);
            break;
        case PORT_IRQ_STAT:
            pr->irq_stat &= ~val;
            break;
        case PORT_IRQ_MASK:
            pr->irq_mask = val & 0xfdc000ff;
            ahci_check_irq(s);
            break;
        case PORT_CMD:
            pr->cmd = val & ~(PORT_CMD_LIST_ON | PORT_CMD_FIS_ON);

            if (pr->cmd & PORT_CMD_START) {
                pr->cmd |= PORT_CMD_LIST_ON;
            }

            if (pr->cmd & PORT_CMD_FIS_RX) {
                pr->cmd |= PORT_CMD_FIS_ON;
            }

            check_cmd(s, port);
            break;
        case PORT_TFDATA:
            s->dev[port].port.ifs[0].error = (val >> 8) & 0xff;
            s->dev[port].port.ifs[0].status = val & 0xff;
            break;
        case PORT_SIG:
            pr->sig = val;
            break;
        case PORT_SCR_STAT:
            pr->scr_stat = val;
            break;
        case PORT_SCR_CTL:
            if (((pr->scr_ctl & AHCI_SCR_SCTL_DET) == 1) &&
                ((val & AHCI_SCR_SCTL_DET) == 0)) {
                ahci_reset_port(s, port);
            }
            pr->scr_ctl = val;
            break;
        case PORT_SCR_ERR:
            pr->scr_err &= ~val;
            break;
        case PORT_SCR_ACT:
            /* RW1 */
            pr->scr_act |= val;
            break;
        case PORT_CMD_ISSUE:
            pr->cmd_issue |= val;
            check_cmd(s, port);
            break;
        default:
            break;
    }
}

static uint32_t ahci_mem_readl(void *ptr, target_phys_addr_t addr)
{
    AHCIState *s = ptr;
    uint32_t val = 0;

    addr = addr & 0xfff;
    if (addr < AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR) {
        switch (addr) {
        case HOST_CAP:
            val = s->control_regs.cap;
            break;
        case HOST_CTL:
            val = s->control_regs.ghc;
            break;
        case HOST_IRQ_STAT:
            val = s->control_regs.irqstatus;
            break;
        case HOST_PORTS_IMPL:
            val = s->control_regs.impl;
            break;
        case HOST_VERSION:
            val = s->control_regs.version;
            break;
        }

        DPRINTF(-1, "(addr 0x%08X), val 0x%08X\n", (unsigned) addr, val);
    } else if ((addr >= AHCI_PORT_REGS_START_ADDR) &&
               (addr < AHCI_PORT_REGS_END_ADDR)) {
        val = ahci_port_read(s, (addr - AHCI_PORT_REGS_START_ADDR) >> 7,
                             addr & AHCI_PORT_ADDR_OFFSET_MASK);
    }

    return val;
}



static void ahci_mem_writel(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    AHCIState *s = ptr;
    addr = addr & 0xfff;

    /* Only aligned reads are allowed on AHCI */
    if (addr & 3) {
        fprintf(stderr, "ahci: Mis-aligned write to addr 0x"
                TARGET_FMT_plx "\n", addr);
        return;
    }

    if (addr < AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR) {
        DPRINTF(-1, "(addr 0x%08X), val 0x%08X\n", (unsigned) addr, val);

        switch (addr) {
            case HOST_CAP: /* R/WO, RO */
                /* FIXME handle R/WO */
                break;
            case HOST_CTL: /* R/W */
                if (val & HOST_CTL_RESET) {
                    DPRINTF(-1, "HBA Reset\n");
                    /* FIXME reset? */
                } else {
                    s->control_regs.ghc = (val & 0x3) | HOST_CTL_AHCI_EN;
                    ahci_check_irq(s);
                }
                break;
            case HOST_IRQ_STAT: /* R/WC, RO */
                s->control_regs.irqstatus &= ~val;
                ahci_check_irq(s);
                break;
            case HOST_PORTS_IMPL: /* R/WO, RO */
                /* FIXME handle R/WO */
                break;
            case HOST_VERSION: /* RO */
                /* FIXME report write? */
                break;
            default:
                DPRINTF(-1, "write to unknown register 0x%x\n", (unsigned)addr);
        }
    } else if ((addr >= AHCI_PORT_REGS_START_ADDR) &&
               (addr < AHCI_PORT_REGS_END_ADDR)) {
        ahci_port_write(s, (addr - AHCI_PORT_REGS_START_ADDR) >> 7,
                        addr & AHCI_PORT_ADDR_OFFSET_MASK, val);
    }

}

static CPUReadMemoryFunc * const ahci_readfn[3]={
    ahci_mem_readl,
    ahci_mem_readl,
    ahci_mem_readl
};

static CPUWriteMemoryFunc * const ahci_writefn[3]={
    ahci_mem_writel,
    ahci_mem_writel,
    ahci_mem_writel
};

static void ahci_reg_init(AHCIState *s)
{
    int i;

    s->control_regs.cap = (SATA_PORTS - 1) |
                          (AHCI_NUM_COMMAND_SLOTS << 8) |
                          (AHCI_SUPPORTED_SPEED_GEN1 << AHCI_SUPPORTED_SPEED) |
                          HOST_CAP_NCQ | HOST_CAP_AHCI;

    s->control_regs.impl = (1 << SATA_PORTS) - 1;

    s->control_regs.version = AHCI_VERSION_1_0;

    for (i = 0; i < SATA_PORTS; i++) {
        s->dev[i].port_state = STATE_RUN;
    }
}

static uint32_t read_from_sglist(uint8_t *buffer, uint32_t len,
                                 QEMUSGList *sglist)
{
    uint32_t i = 0;
    uint32_t total = 0, once;
    ScatterGatherEntry *cur_prd;
    uint32_t sgcount;

    cur_prd = sglist->sg;
    sgcount = sglist->nsg;
    for (i = 0; len && sgcount; i++) {
        once = MIN(cur_prd->len, len);
        cpu_physical_memory_read(cur_prd->base, buffer, once);
        cur_prd++;
        sgcount--;
        len -= once;
        buffer += once;
        total += once;
    }

    return total;
}

static uint32_t write_to_sglist(uint8_t *buffer, uint32_t len,
                                QEMUSGList *sglist)
{
    uint32_t i = 0;
    uint32_t total = 0, once;
    ScatterGatherEntry *cur_prd;
    uint32_t sgcount;

    DPRINTF(-1, "total: 0x%x bytes\n", len);

    cur_prd = sglist->sg;
    sgcount = sglist->nsg;
    for (i = 0; len && sgcount; i++) {
        once = MIN(cur_prd->len, len);
        DPRINTF(-1, "write 0x%x bytes to 0x%lx\n", once, (long)cur_prd->base);
        cpu_physical_memory_write(cur_prd->base, buffer, once);
        cur_prd++;
        sgcount--;
        len -= once;
        buffer += once;
        total += once;
    }

    return total;
}

static void check_cmd(AHCIState *s, int port)
{
    AHCIPortRegs *pr = &s->dev[port].port_regs;
    int slot;

    if ((pr->cmd & PORT_CMD_START) && pr->cmd_issue) {
        for (slot = 0; (slot < 32) && pr->cmd_issue; slot++) {
            if ((pr->cmd_issue & (1 << slot)) &&
                !handle_cmd(s, port, slot)) {
                pr->cmd_issue &= ~(1 << slot);
            }
        }
    }
}

static void ahci_check_cmd_bh(void *opaque)
{
    AHCIDevice *ad = opaque;

    qemu_bh_delete(ad->check_bh);
    ad->check_bh = NULL;

    if ((ad->busy_slot != -1) &&
        !(ad->port.ifs[0].status & (BUSY_STAT|DRQ_STAT))) {
        /* no longer busy */
        ad->port_regs.cmd_issue &= ~(1 << ad->busy_slot);
        ad->busy_slot = -1;
    }

    check_cmd(ad->hba, ad->port_no);
}

static void ahci_reset_port(AHCIState *s, int port)
{
    AHCIDevice *d = &s->dev[port];
    AHCIPortRegs *pr = &d->port_regs;
    IDEState *ide_state = &d->port.ifs[0];
    uint8_t init_fis[0x20];
    int i;

    DPRINTF(port, "reset port\n");

    ide_bus_reset(&d->port);
    ide_state->ncq_queues = AHCI_MAX_CMDS;

    pr->irq_stat = 0;
    pr->irq_mask = 0;
    pr->scr_stat = 0;
    pr->scr_ctl = 0;
    pr->scr_err = 0;
    pr->scr_act = 0;
    d->busy_slot = -1;

    ide_state = &s->dev[port].port.ifs[0];
    if (!ide_state->bs) {
        return;
    }

    /* reset ncq queue */
    for (i = 0; i < AHCI_MAX_CMDS; i++) {
        NCQTransferState *ncq_tfs = &s->dev[port].ncq_tfs[i];
        if (!ncq_tfs->used) {
            continue;
        }

        if (ncq_tfs->aiocb) {
            bdrv_aio_cancel(ncq_tfs->aiocb);
            ncq_tfs->aiocb = NULL;
        }

        qemu_sglist_destroy(&ncq_tfs->sglist);
        ncq_tfs->used = 0;
    }

    memset(init_fis, 0, sizeof(init_fis));
    s->dev[port].port_state = STATE_RUN;
    if (!ide_state->bs) {
        s->dev[port].port_regs.sig = 0;
        ide_state->status = SEEK_STAT | WRERR_STAT;
    } else if (ide_state->drive_kind == IDE_CD) {
        s->dev[port].port_regs.sig = SATA_SIGNATURE_CDROM;
        ide_state->lcyl = 0x14;
        ide_state->hcyl = 0xeb;
        DPRINTF(port, "set lcyl = %d\n", ide_state->lcyl);
        init_fis[5] = ide_state->lcyl;
        init_fis[6] = ide_state->hcyl;
        ide_state->status = SEEK_STAT | WRERR_STAT | READY_STAT;
    } else {
        s->dev[port].port_regs.sig = SATA_SIGNATURE_DISK;
        ide_state->status = SEEK_STAT | WRERR_STAT;
    }

    ide_state->error = 1;
    init_fis[4] = 1;
    init_fis[12] = 1;
    ahci_write_fis_d2h(d, init_fis);
}

static void debug_print_fis(uint8_t *fis, int cmd_len)
{
#ifdef DEBUG_AHCI
    int i;

    fprintf(stderr, "fis:");
    for (i = 0; i < cmd_len; i++) {
        if ((i & 0xf) == 0) {
            fprintf(stderr, "\n%02x:",i);
        }
        fprintf(stderr, "%02x ",fis[i]);
    }
    fprintf(stderr, "\n");
#endif
}

static void ahci_write_fis_sdb(AHCIState *s, int port, uint32_t finished)
{
    AHCIPortRegs *pr = &s->dev[port].port_regs;
    IDEState *ide_state;
    uint8_t *sdb_fis;

    if (!s->dev[port].res_fis ||
        !(pr->cmd & PORT_CMD_FIS_RX)) {
        return;
    }

    sdb_fis = &s->dev[port].res_fis[RES_FIS_SDBFIS];
    ide_state = &s->dev[port].port.ifs[0];

    /* clear memory */
    *(uint32_t*)sdb_fis = 0;

    /* write values */
    sdb_fis[0] = ide_state->error;
    sdb_fis[2] = ide_state->status & 0x77;
    s->dev[port].finished |= finished;
    *(uint32_t*)(sdb_fis + 4) = cpu_to_le32(s->dev[port].finished);

    ahci_trigger_irq(s, &s->dev[port], PORT_IRQ_STAT_SDBS);
}

static void ahci_write_fis_d2h(AHCIDevice *ad, uint8_t *cmd_fis)
{
    AHCIPortRegs *pr = &ad->port_regs;
    uint8_t *d2h_fis;
    int i;
    target_phys_addr_t cmd_len = 0x80;
    int cmd_mapped = 0;

    if (!ad->res_fis || !(pr->cmd & PORT_CMD_FIS_RX)) {
        return;
    }

    if (!cmd_fis) {
        /* map cmd_fis */
        uint64_t tbl_addr = le64_to_cpu(ad->cur_cmd->tbl_addr);
        cmd_fis = cpu_physical_memory_map(tbl_addr, &cmd_len, 0);
        cmd_mapped = 1;
    }

    d2h_fis = &ad->res_fis[RES_FIS_RFIS];

    d2h_fis[0] = 0x34;
    d2h_fis[1] = (ad->hba->control_regs.irqstatus ? (1 << 6) : 0);
    d2h_fis[2] = ad->port.ifs[0].status;
    d2h_fis[3] = ad->port.ifs[0].error;

    d2h_fis[4] = cmd_fis[4];
    d2h_fis[5] = cmd_fis[5];
    d2h_fis[6] = cmd_fis[6];
    d2h_fis[7] = cmd_fis[7];
    d2h_fis[8] = cmd_fis[8];
    d2h_fis[9] = cmd_fis[9];
    d2h_fis[10] = cmd_fis[10];
    d2h_fis[11] = cmd_fis[11];
    d2h_fis[12] = cmd_fis[12];
    d2h_fis[13] = cmd_fis[13];
    for (i = 14; i < 0x20; i++) {
        d2h_fis[i] = 0;
    }

    if (d2h_fis[2] & ERR_STAT) {
        ahci_trigger_irq(ad->hba, ad, PORT_IRQ_STAT_TFES);
    }

    ahci_trigger_irq(ad->hba, ad, PORT_IRQ_D2H_REG_FIS);

    if (cmd_mapped) {
        cpu_physical_memory_unmap(cmd_fis, 0, cmd_len, cmd_len);
    }
}

static int ahci_populate_sglist(AHCIDevice *ad, QEMUSGList *sglist)
{
    AHCICmdHdr *cmd = ad->cur_cmd;
    uint32_t opts = le32_to_cpu(cmd->opts);
    uint64_t prdt_addr = le64_to_cpu(cmd->tbl_addr) + 0x80;
    int sglist_alloc_hint = opts >> AHCI_CMD_HDR_PRDT_LEN;
    target_phys_addr_t prdt_len = (sglist_alloc_hint * sizeof(AHCI_SG));
    target_phys_addr_t real_prdt_len = prdt_len;
    uint8_t *prdt;
    int i;
    int r = 0;

    if (!sglist_alloc_hint) {
        DPRINTF(ad->port_no, "no sg list given by guest: 0x%08x\n", opts);
        return -1;
    }

    /* map PRDT */
    if (!(prdt = cpu_physical_memory_map(prdt_addr, &prdt_len, 0))){
        DPRINTF(ad->port_no, "map failed\n");
        return -1;
    }

    if (prdt_len < real_prdt_len) {
        DPRINTF(ad->port_no, "mapped less than expected\n");
        r = -1;
        goto out;
    }

    /* Get entries in the PRDT, init a qemu sglist accordingly */
    if (sglist_alloc_hint > 0) {
        AHCI_SG *tbl = (AHCI_SG *)prdt;

        qemu_sglist_init(sglist, sglist_alloc_hint);
        for (i = 0; i < sglist_alloc_hint; i++) {
            /* flags_size is zero-based */
            qemu_sglist_add(sglist, le64_to_cpu(tbl[i].addr),
                            le32_to_cpu(tbl[i].flags_size) + 1);
        }
    }

out:
    cpu_physical_memory_unmap(prdt, 0, prdt_len, prdt_len);
    return r;
}

static void ncq_cb(void *opaque, int ret)
{
    NCQTransferState *ncq_tfs = (NCQTransferState *)opaque;
    IDEState *ide_state = &ncq_tfs->drive->port.ifs[0];

    /* Clear bit for this tag in SActive */
    ncq_tfs->drive->port_regs.scr_act &= ~(1 << ncq_tfs->tag);

    if (ret < 0) {
        /* error */
        ide_state->error = ABRT_ERR;
        ide_state->status = READY_STAT | ERR_STAT;
        ncq_tfs->drive->port_regs.scr_err |= (1 << ncq_tfs->tag);
    } else {
        ide_state->status = READY_STAT | SEEK_STAT;
    }

    ahci_write_fis_sdb(ncq_tfs->drive->hba, ncq_tfs->drive->port_no,
                       (1 << ncq_tfs->tag));

    DPRINTF(ncq_tfs->drive->port_no, "NCQ transfer tag %d finished\n",
            ncq_tfs->tag);

    qemu_sglist_destroy(&ncq_tfs->sglist);
    ncq_tfs->used = 0;
}

static void process_ncq_command(AHCIState *s, int port, uint8_t *cmd_fis,
                                int slot)
{
    NCQFrame *ncq_fis = (NCQFrame*)cmd_fis;
    uint8_t tag = ncq_fis->tag >> 3;
    NCQTransferState *ncq_tfs = &s->dev[port].ncq_tfs[tag];

    if (ncq_tfs->used) {
        /* error - already in use */
        fprintf(stderr, "%s: tag %d already used\n", __FUNCTION__, tag);
        return;
    }

    ncq_tfs->used = 1;
    ncq_tfs->drive = &s->dev[port];
    ncq_tfs->slot = slot;
    ncq_tfs->lba = ((uint64_t)ncq_fis->lba5 << 40) |
                   ((uint64_t)ncq_fis->lba4 << 32) |
                   ((uint64_t)ncq_fis->lba3 << 24) |
                   ((uint64_t)ncq_fis->lba2 << 16) |
                   ((uint64_t)ncq_fis->lba1 << 8) |
                   (uint64_t)ncq_fis->lba0;

    /* Note: We calculate the sector count, but don't currently rely on it.
     * The total size of the DMA buffer tells us the transfer size instead. */
    ncq_tfs->sector_count = ((uint16_t)ncq_fis->sector_count_high << 8) |
                                ncq_fis->sector_count_low;

    DPRINTF(port, "NCQ transfer LBA from %ld to %ld, drive max %ld\n",
            ncq_tfs->lba, ncq_tfs->lba + ncq_tfs->sector_count - 2,
            s->dev[port].port.ifs[0].nb_sectors - 1);

    ahci_populate_sglist(&s->dev[port], &ncq_tfs->sglist);
    ncq_tfs->tag = tag;

    switch(ncq_fis->command) {
        case READ_FPDMA_QUEUED:
            DPRINTF(port, "NCQ reading %d sectors from LBA %ld, tag %d\n",
                    ncq_tfs->sector_count-1, ncq_tfs->lba, ncq_tfs->tag);
            ncq_tfs->is_read = 1;

            DPRINTF(port, "tag %d aio read %ld\n", ncq_tfs->tag, ncq_tfs->lba);
            ncq_tfs->aiocb = dma_bdrv_read(ncq_tfs->drive->port.ifs[0].bs,
                                           &ncq_tfs->sglist, ncq_tfs->lba,
                                           ncq_cb, ncq_tfs);
            break;
        case WRITE_FPDMA_QUEUED:
            DPRINTF(port, "NCQ writing %d sectors to LBA %ld, tag %d\n",
                    ncq_tfs->sector_count-1, ncq_tfs->lba, ncq_tfs->tag);
            ncq_tfs->is_read = 0;

            DPRINTF(port, "tag %d aio write %ld\n", ncq_tfs->tag, ncq_tfs->lba);
            ncq_tfs->aiocb = dma_bdrv_write(ncq_tfs->drive->port.ifs[0].bs,
                                            &ncq_tfs->sglist, ncq_tfs->lba,
                                            ncq_cb, ncq_tfs);
            break;
        default:
            DPRINTF(port, "error: tried to process non-NCQ command as NCQ\n");
            qemu_sglist_destroy(&ncq_tfs->sglist);
            break;
    }
}

static int handle_cmd(AHCIState *s, int port, int slot)
{
    IDEState *ide_state;
    uint32_t opts;
    uint64_t tbl_addr;
    AHCICmdHdr *cmd;
    uint8_t *cmd_fis;
    target_phys_addr_t cmd_len;

    if (s->dev[port].port.ifs[0].status & (BUSY_STAT|DRQ_STAT)) {
        /* Engine currently busy, try again later */
        DPRINTF(port, "engine busy\n");
        return -1;
    }

    cmd = &((AHCICmdHdr *)s->dev[port].lst)[slot];

    if (!s->dev[port].lst) {
        DPRINTF(port, "error: lst not given but cmd handled");
        return -1;
    }

    /* remember current slot handle for later */
    s->dev[port].cur_cmd = cmd;

    opts = le32_to_cpu(cmd->opts);
    tbl_addr = le64_to_cpu(cmd->tbl_addr);

    cmd_len = 0x80;
    cmd_fis = cpu_physical_memory_map(tbl_addr, &cmd_len, 1);

    if (!cmd_fis) {
        DPRINTF(port, "error: guest passed us an invalid cmd fis\n");
        return -1;
    }

    /* The device we are working for */
    ide_state = &s->dev[port].port.ifs[0];

    if (!ide_state->bs) {
        DPRINTF(port, "error: guest accessed unused port");
        goto out;
    }

    debug_print_fis(cmd_fis, 0x90);
    //debug_print_fis(cmd_fis, (opts & AHCI_CMD_HDR_CMD_FIS_LEN) * 4);

    switch (cmd_fis[0]) {
        case SATA_FIS_TYPE_REGISTER_H2D:
            break;
        default:
            DPRINTF(port, "unknown command cmd_fis[0]=%02x cmd_fis[1]=%02x "
                          "cmd_fis[2]=%02x\n", cmd_fis[0], cmd_fis[1],
                          cmd_fis[2]);
            goto out;
            break;
    }

    switch (cmd_fis[1]) {
        case SATA_FIS_REG_H2D_UPDATE_COMMAND_REGISTER:
            break;
        case 0:
            break;
        default:
            DPRINTF(port, "unknown command cmd_fis[0]=%02x cmd_fis[1]=%02x "
                          "cmd_fis[2]=%02x\n", cmd_fis[0], cmd_fis[1],
                          cmd_fis[2]);
            goto out;
            break;
    }

    switch (s->dev[port].port_state) {
        case STATE_RUN:
            if (cmd_fis[15] & ATA_SRST) {
                s->dev[port].port_state = STATE_RESET;
            }
            break;
        case STATE_RESET:
            if (!(cmd_fis[15] & ATA_SRST)) {
                ahci_reset_port(s, port);
            }
            break;
    }

    if (cmd_fis[1] == SATA_FIS_REG_H2D_UPDATE_COMMAND_REGISTER) {

        /* Check for NCQ command */
        if ((cmd_fis[2] == READ_FPDMA_QUEUED) ||
            (cmd_fis[2] == WRITE_FPDMA_QUEUED)) {
            process_ncq_command(s, port, cmd_fis, slot);
            goto out;
        }

        /* Decompose the FIS  */
        ide_state->nsector = (int64_t)((cmd_fis[13] << 8) | cmd_fis[12]);
        ide_state->feature = cmd_fis[3];
        if (!ide_state->nsector) {
            ide_state->nsector = 256;
        }

        if (ide_state->drive_kind != IDE_CD) {
            ide_set_sector(ide_state, (cmd_fis[6] << 16) | (cmd_fis[5] << 8) |
                           cmd_fis[4]);
        }

        /* Copy the ACMD field (ATAPI packet, if any) from the AHCI command
         * table to ide_state->io_buffer
         */
        if (opts & AHCI_CMD_ATAPI) {
            memcpy(ide_state->io_buffer, &cmd_fis[AHCI_COMMAND_TABLE_ACMD], 0x10);
            ide_state->lcyl = 0x14;
            ide_state->hcyl = 0xeb;
            debug_print_fis(ide_state->io_buffer, 0x10);
            ide_state->feature = IDE_FEATURE_DMA;
            s->dev[port].done_atapi_packet = 0;
            /* XXX send PIO setup FIS */
        }

        ide_state->error = 0;

        /* Reset transferred byte counter */
        cmd->status = 0;

        /* We're ready to process the command in FIS byte 2. */
        ide_exec_cmd(&s->dev[port].port, cmd_fis[2]);

        if (s->dev[port].port.ifs[0].status & READY_STAT) {
            ahci_write_fis_d2h(&s->dev[port], cmd_fis);
        }
    }

out:
    cpu_physical_memory_unmap(cmd_fis, 1, cmd_len, cmd_len);

    if (s->dev[port].port.ifs[0].status & (BUSY_STAT|DRQ_STAT)) {
        /* async command, complete later */
        s->dev[port].busy_slot = slot;
        return -1;
    }

    /* done handling the command */
    return 0;
}

/* DMA dev <-> ram */
static int ahci_start_transfer(IDEDMA *dma)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    IDEState *s = &ad->port.ifs[0];
    uint32_t size = (uint32_t)(s->data_end - s->data_ptr);
    /* write == ram -> device */
    uint32_t opts = le32_to_cpu(ad->cur_cmd->opts);
    int is_write = opts & AHCI_CMD_WRITE;
    int is_atapi = opts & AHCI_CMD_ATAPI;
    int has_sglist = 0;

    if (is_atapi && !ad->done_atapi_packet) {
        /* already prepopulated iobuffer */
        ad->done_atapi_packet = 1;
        goto out;
    }

    if (!ahci_populate_sglist(ad, &s->sg)) {
        has_sglist = 1;
    }

    DPRINTF(ad->port_no, "%sing %d bytes on %s w/%s sglist\n",
            is_write ? "writ" : "read", size, is_atapi ? "atapi" : "ata",
            has_sglist ? "" : "o");

    if (is_write && has_sglist && (s->data_ptr < s->data_end)) {
        read_from_sglist(s->data_ptr, size, &s->sg);
    }

    if (!is_write && has_sglist && (s->data_ptr < s->data_end)) {
        write_to_sglist(s->data_ptr, size, &s->sg);
    }

    /* update number of transferred bytes */
    ad->cur_cmd->status = cpu_to_le32(le32_to_cpu(ad->cur_cmd->status) + size);

out:
    /* declare that we processed everything */
    s->data_ptr = s->data_end;

    if (has_sglist) {
        qemu_sglist_destroy(&s->sg);
    }

    s->end_transfer_func(s);

    if (!(s->status & DRQ_STAT)) {
        /* done with DMA */
        ahci_trigger_irq(ad->hba, ad, PORT_IRQ_STAT_DSS);
    }

    return 0;
}

static void ahci_start_dma(IDEDMA *dma, IDEState *s,
                           BlockDriverCompletionFunc *dma_cb)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);

    DPRINTF(ad->port_no, "\n");
    ad->dma_cb = dma_cb;
    ad->dma_status |= BM_STATUS_DMAING;
    dma_cb(s, 0);
}

static int ahci_dma_prepare_buf(IDEDMA *dma, int is_write)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    IDEState *s = &ad->port.ifs[0];
    int i;

    ahci_populate_sglist(ad, &s->sg);

    s->io_buffer_size = 0;
    for (i = 0; i < s->sg.nsg; i++) {
        s->io_buffer_size += s->sg.sg[i].len;
    }

    DPRINTF(ad->port_no, "len=%#x\n", s->io_buffer_size);
    return s->io_buffer_size != 0;
}

static int ahci_dma_rw_buf(IDEDMA *dma, int is_write)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    IDEState *s = &ad->port.ifs[0];
    uint8_t *p = s->io_buffer + s->io_buffer_index;
    int l = s->io_buffer_size - s->io_buffer_index;

    if (ahci_populate_sglist(ad, &s->sg)) {
        return 0;
    }

    if (is_write) {
        write_to_sglist(p, l, &s->sg);
    } else {
        read_from_sglist(p, l, &s->sg);
    }

    /* update number of transferred bytes */
    ad->cur_cmd->status = cpu_to_le32(le32_to_cpu(ad->cur_cmd->status) + l);
    s->io_buffer_index += l;

    DPRINTF(ad->port_no, "len=%#x\n", l);

    return 1;
}

static int ahci_dma_set_unit(IDEDMA *dma, int unit)
{
    /* only a single unit per link */
    return 0;
}

static int ahci_dma_add_status(IDEDMA *dma, int status)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    ad->dma_status |= status;
    DPRINTF(ad->port_no, "set status: %x\n", status);

    if (status & BM_STATUS_INT) {
        ahci_trigger_irq(ad->hba, ad, PORT_IRQ_STAT_DSS);
    }

    return 0;
}

static int ahci_dma_set_inactive(IDEDMA *dma)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);

    DPRINTF(ad->port_no, "dma done\n");

    /* update d2h status */
    ahci_write_fis_d2h(ad, NULL);

    ad->dma_cb = NULL;

    /* maybe we still have something to process, check later */
    ad->check_bh = qemu_bh_new(ahci_check_cmd_bh, ad);
    qemu_bh_schedule(ad->check_bh);

    return 0;
}

static void ahci_irq_set(void *opaque, int n, int level)
{
}

static void ahci_dma_restart_cb(void *opaque, int running, int reason)
{
}

static int ahci_dma_reset(IDEDMA *dma)
{
    return 0;
}

static const IDEDMAOps ahci_dma_ops = {
    .start_dma = ahci_start_dma,
    .start_transfer = ahci_start_transfer,
    .prepare_buf = ahci_dma_prepare_buf,
    .rw_buf = ahci_dma_rw_buf,
    .set_unit = ahci_dma_set_unit,
    .add_status = ahci_dma_add_status,
    .set_inactive = ahci_dma_set_inactive,
    .restart_cb = ahci_dma_restart_cb,
    .reset = ahci_dma_reset,
};

static void ahci_init(AHCIState *s, DeviceState *qdev)
{
    qemu_irq *irqs;
    int i;

    ahci_reg_init(s);
    s->mem = cpu_register_io_memory(ahci_readfn, ahci_writefn, s,
                                    DEVICE_LITTLE_ENDIAN);
    irqs = qemu_allocate_irqs(ahci_irq_set, s, SATA_PORTS);

    for (i = 0; i < SATA_PORTS; i++) {
        AHCIDevice *ad = &s->dev[i];

        ide_bus_new(&ad->port, qdev, i);
        ide_init2(&ad->port, irqs[i]);

        ad->hba = s;
        ad->port_no = i;
        ad->port.dma = &ad->dma;
        ad->port.dma->ops = &ahci_dma_ops;
        ad->port_regs.cmd = PORT_CMD_SPIN_UP | PORT_CMD_POWER_ON;
    }
}

static void ahci_pci_map(PCIDevice *pci_dev, int region_num,
        pcibus_t addr, pcibus_t size, int type)
{
    struct AHCIPCIState *d = (struct AHCIPCIState *)pci_dev;
    AHCIState *s = &d->ahci;

    cpu_register_physical_memory(addr, size, s->mem);
}

static void ahci_reset(void *opaque)
{
    struct AHCIPCIState *d = opaque;
    int i;

    for (i = 0; i < SATA_PORTS; i++) {
        ahci_reset_port(&d->ahci, i);
    }
}

static int pci_ahci_init(PCIDevice *dev)
{
    struct AHCIPCIState *d;
    d = DO_UPCAST(struct AHCIPCIState, card, dev);

    pci_config_set_vendor_id(d->card.config, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(d->card.config, PCI_DEVICE_ID_INTEL_82801IR);

    pci_config_set_class(d->card.config, PCI_CLASS_STORAGE_SATA);
    pci_config_set_revision(d->card.config, 0x02);
    pci_config_set_prog_interface(d->card.config, AHCI_PROGMODE_MAJOR_REV_1);

    d->card.config[PCI_CACHE_LINE_SIZE] = 0x08;  /* Cache line size */
    d->card.config[PCI_LATENCY_TIMER]   = 0x00;  /* Latency timer */
    pci_config_set_interrupt_pin(d->card.config, 1);

    /* XXX Software should program this register */
    d->card.config[0x90]   = 1 << 6; /* Address Map Register - AHCI mode */

    qemu_register_reset(ahci_reset, d);

    /* XXX BAR size should be 1k, but that breaks, so bump it to 4k for now */
    pci_register_bar(&d->card, 5, 0x1000, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     ahci_pci_map);

    msi_init(dev, 0x50, 1, true, false);

    ahci_init(&d->ahci, &dev->qdev);
    d->ahci.irq = d->card.irq[0];

    return 0;
}

static int pci_ahci_uninit(PCIDevice *dev)
{
    struct AHCIPCIState *d;
    d = DO_UPCAST(struct AHCIPCIState, card, dev);

    if (msi_enabled(dev)) {
        msi_uninit(dev);
    }

    qemu_unregister_reset(ahci_reset, d);

    return 0;
}

static void pci_ahci_write_config(PCIDevice *pci, uint32_t addr,
                                  uint32_t val, int len)
{
    pci_default_write_config(pci, addr, val, len);
    msi_write_config(pci, addr, val, len);
}

static PCIDeviceInfo ahci_info = {
    .qdev.name  = "ahci",
    .qdev.size  = sizeof(AHCIPCIState),
    .init       = pci_ahci_init,
    .exit       = pci_ahci_uninit,
    .config_write = pci_ahci_write_config,
};

static void ahci_pci_register_devices(void)
{
    pci_qdev_register(&ahci_info);
}

device_init(ahci_pci_register_devices)
