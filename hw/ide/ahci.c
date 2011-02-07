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

#include <hw/hw.h>
#include <hw/msi.h>
#include <hw/pc.h>
#include <hw/pci.h>

#include "monitor.h"
#include "dma.h"
#include "cpu-common.h"
#include "internal.h"
#include <hw/ide/pci.h>
#include <hw/ide/ahci.h>

/* #define DEBUG_AHCI */

#ifdef DEBUG_AHCI
#define DPRINTF(port, fmt, ...) \
do { fprintf(stderr, "ahci: %s: [%d] ", __FUNCTION__, port); \
     fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(port, fmt, ...) do {} while(0)
#endif

static void check_cmd(AHCIState *s, int port);
static int handle_cmd(AHCIState *s,int port,int slot);
static void ahci_reset_port(AHCIState *s, int port);
static void ahci_write_fis_d2h(AHCIDevice *ad, uint8_t *cmd_fis);
static void ahci_init_d2h(AHCIDevice *ad);

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

    for (i = 0; i < s->ports; i++) {
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
        cpu_physical_memory_unmap(*ptr, len, 1, len);
    }

    *ptr = cpu_physical_memory_map(addr, &len, 1);
    if (len < wanted) {
        cpu_physical_memory_unmap(*ptr, len, 1, len);
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

            /* XXX usually the FIS would be pending on the bus here and
                   issuing deferred until the OS enables FIS receival.
                   Instead, we only submit it once - which works in most
                   cases, but is a hack. */
            if ((pr->cmd & PORT_CMD_FIS_ON) &&
                !s->dev[port].init_d2h_sent) {
                ahci_init_d2h(&s->dev[port]);
                s->dev[port].init_d2h_sent = 1;
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
               (addr < (AHCI_PORT_REGS_START_ADDR +
                (s->ports * AHCI_PORT_ADDR_OFFSET_LEN)))) {
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
                    ahci_reset(container_of(s, AHCIPCIState, ahci));
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
               (addr < (AHCI_PORT_REGS_START_ADDR +
                (s->ports * AHCI_PORT_ADDR_OFFSET_LEN)))) {
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

    s->control_regs.cap = (s->ports - 1) |
                          (AHCI_NUM_COMMAND_SLOTS << 8) |
                          (AHCI_SUPPORTED_SPEED_GEN1 << AHCI_SUPPORTED_SPEED) |
                          HOST_CAP_NCQ | HOST_CAP_AHCI;

    s->control_regs.impl = (1 << s->ports) - 1;

    s->control_regs.version = AHCI_VERSION_1_0;

    for (i = 0; i < s->ports; i++) {
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

static void ahci_init_d2h(AHCIDevice *ad)
{
    uint8_t init_fis[0x20];
    IDEState *ide_state = &ad->port.ifs[0];

    memset(init_fis, 0, sizeof(init_fis));

    init_fis[4] = 1;
    init_fis[12] = 1;

    if (ide_state->drive_kind == IDE_CD) {
        init_fis[5] = ide_state->lcyl;
        init_fis[6] = ide_state->hcyl;
    }

    ahci_write_fis_d2h(ad, init_fis);
}

static void ahci_reset_port(AHCIState *s, int port)
{
    AHCIDevice *d = &s->dev[port];
    AHCIPortRegs *pr = &d->port_regs;
    IDEState *ide_state = &d->port.ifs[0];
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
    d->init_d2h_sent = 0;

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

    s->dev[port].port_state = STATE_RUN;
    if (!ide_state->bs) {
        s->dev[port].port_regs.sig = 0;
        ide_state->status = SEEK_STAT | WRERR_STAT;
    } else if (ide_state->drive_kind == IDE_CD) {
        s->dev[port].port_regs.sig = SATA_SIGNATURE_CDROM;
        ide_state->lcyl = 0x14;
        ide_state->hcyl = 0xeb;
        DPRINTF(port, "set lcyl = %d\n", ide_state->lcyl);
        ide_state->status = SEEK_STAT | WRERR_STAT | READY_STAT;
    } else {
        s->dev[port].port_regs.sig = SATA_SIGNATURE_DISK;
        ide_state->status = SEEK_STAT | WRERR_STAT;
    }

    ide_state->error = 1;
    ahci_init_d2h(d);
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
        cpu_physical_memory_unmap(cmd_fis, cmd_len, 0, cmd_len);
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
    cpu_physical_memory_unmap(prdt, prdt_len, 0, prdt_len);
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
    cpu_physical_memory_unmap(cmd_fis, cmd_len, 1, cmd_len);

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

void ahci_init(AHCIState *s, DeviceState *qdev, int ports)
{
    qemu_irq *irqs;
    int i;

    s->ports = ports;
    s->dev = qemu_mallocz(sizeof(AHCIDevice) * ports);
    ahci_reg_init(s);
    s->mem = cpu_register_io_memory(ahci_readfn, ahci_writefn, s,
                                    DEVICE_LITTLE_ENDIAN);
    irqs = qemu_allocate_irqs(ahci_irq_set, s, s->ports);

    for (i = 0; i < s->ports; i++) {
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

void ahci_uninit(AHCIState *s)
{
    qemu_free(s->dev);
}

void ahci_pci_map(PCIDevice *pci_dev, int region_num,
        pcibus_t addr, pcibus_t size, int type)
{
    struct AHCIPCIState *d = (struct AHCIPCIState *)pci_dev;
    AHCIState *s = &d->ahci;

    cpu_register_physical_memory(addr, size, s->mem);
}

void ahci_reset(void *opaque)
{
    struct AHCIPCIState *d = opaque;
    int i;

    d->ahci.control_regs.irqstatus = 0;
    d->ahci.control_regs.ghc = 0;

    for (i = 0; i < d->ahci.ports; i++) {
        ahci_reset_port(&d->ahci, i);
    }
}
