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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "sysemu/block-backend.h"
#include "sysemu/dma.h"
#include "hw/ide/internal.h"
#include "hw/ide/pci.h"
#include "ahci_internal.h"

#include "trace.h"

static void check_cmd(AHCIState *s, int port);
static int handle_cmd(AHCIState *s, int port, uint8_t slot);
static void ahci_reset_port(AHCIState *s, int port);
static bool ahci_write_fis_d2h(AHCIDevice *ad);
static void ahci_init_d2h(AHCIDevice *ad);
static int ahci_dma_prepare_buf(const IDEDMA *dma, int32_t limit);
static bool ahci_map_clb_address(AHCIDevice *ad);
static bool ahci_map_fis_address(AHCIDevice *ad);
static void ahci_unmap_clb_address(AHCIDevice *ad);
static void ahci_unmap_fis_address(AHCIDevice *ad);

static const char *AHCIHostReg_lookup[AHCI_HOST_REG__COUNT] = {
    [AHCI_HOST_REG_CAP]        = "CAP",
    [AHCI_HOST_REG_CTL]        = "GHC",
    [AHCI_HOST_REG_IRQ_STAT]   = "IS",
    [AHCI_HOST_REG_PORTS_IMPL] = "PI",
    [AHCI_HOST_REG_VERSION]    = "VS",
    [AHCI_HOST_REG_CCC_CTL]    = "CCC_CTL",
    [AHCI_HOST_REG_CCC_PORTS]  = "CCC_PORTS",
    [AHCI_HOST_REG_EM_LOC]     = "EM_LOC",
    [AHCI_HOST_REG_EM_CTL]     = "EM_CTL",
    [AHCI_HOST_REG_CAP2]       = "CAP2",
    [AHCI_HOST_REG_BOHC]       = "BOHC",
};

static const char *AHCIPortReg_lookup[AHCI_PORT_REG__COUNT] = {
    [AHCI_PORT_REG_LST_ADDR]    = "PxCLB",
    [AHCI_PORT_REG_LST_ADDR_HI] = "PxCLBU",
    [AHCI_PORT_REG_FIS_ADDR]    = "PxFB",
    [AHCI_PORT_REG_FIS_ADDR_HI] = "PxFBU",
    [AHCI_PORT_REG_IRQ_STAT]    = "PxIS",
    [AHCI_PORT_REG_IRQ_MASK]    = "PXIE",
    [AHCI_PORT_REG_CMD]         = "PxCMD",
    [7]                         = "Reserved",
    [AHCI_PORT_REG_TFDATA]      = "PxTFD",
    [AHCI_PORT_REG_SIG]         = "PxSIG",
    [AHCI_PORT_REG_SCR_STAT]    = "PxSSTS",
    [AHCI_PORT_REG_SCR_CTL]     = "PxSCTL",
    [AHCI_PORT_REG_SCR_ERR]     = "PxSERR",
    [AHCI_PORT_REG_SCR_ACT]     = "PxSACT",
    [AHCI_PORT_REG_CMD_ISSUE]   = "PxCI",
    [AHCI_PORT_REG_SCR_NOTIF]   = "PxSNTF",
    [AHCI_PORT_REG_FIS_CTL]     = "PxFBS",
    [AHCI_PORT_REG_DEV_SLEEP]   = "PxDEVSLP",
    [18 ... 27]                 = "Reserved",
    [AHCI_PORT_REG_VENDOR_1 ...
     AHCI_PORT_REG_VENDOR_4]    = "PxVS",
};

static const char *AHCIPortIRQ_lookup[AHCI_PORT_IRQ__COUNT] = {
    [AHCI_PORT_IRQ_BIT_DHRS] = "DHRS",
    [AHCI_PORT_IRQ_BIT_PSS]  = "PSS",
    [AHCI_PORT_IRQ_BIT_DSS]  = "DSS",
    [AHCI_PORT_IRQ_BIT_SDBS] = "SDBS",
    [AHCI_PORT_IRQ_BIT_UFS]  = "UFS",
    [AHCI_PORT_IRQ_BIT_DPS]  = "DPS",
    [AHCI_PORT_IRQ_BIT_PCS]  = "PCS",
    [AHCI_PORT_IRQ_BIT_DMPS] = "DMPS",
    [8 ... 21]               = "RESERVED",
    [AHCI_PORT_IRQ_BIT_PRCS] = "PRCS",
    [AHCI_PORT_IRQ_BIT_IPMS] = "IPMS",
    [AHCI_PORT_IRQ_BIT_OFS]  = "OFS",
    [25]                     = "RESERVED",
    [AHCI_PORT_IRQ_BIT_INFS] = "INFS",
    [AHCI_PORT_IRQ_BIT_IFS]  = "IFS",
    [AHCI_PORT_IRQ_BIT_HBDS] = "HBDS",
    [AHCI_PORT_IRQ_BIT_HBFS] = "HBFS",
    [AHCI_PORT_IRQ_BIT_TFES] = "TFES",
    [AHCI_PORT_IRQ_BIT_CPDS] = "CPDS"
};

static uint32_t ahci_port_read(AHCIState *s, int port, int offset)
{
    uint32_t val;
    AHCIPortRegs *pr = &s->dev[port].port_regs;
    enum AHCIPortReg regnum = offset / sizeof(uint32_t);
    assert(regnum < (AHCI_PORT_ADDR_OFFSET_LEN / sizeof(uint32_t)));

    switch (regnum) {
    case AHCI_PORT_REG_LST_ADDR:
        val = pr->lst_addr;
        break;
    case AHCI_PORT_REG_LST_ADDR_HI:
        val = pr->lst_addr_hi;
        break;
    case AHCI_PORT_REG_FIS_ADDR:
        val = pr->fis_addr;
        break;
    case AHCI_PORT_REG_FIS_ADDR_HI:
        val = pr->fis_addr_hi;
        break;
    case AHCI_PORT_REG_IRQ_STAT:
        val = pr->irq_stat;
        break;
    case AHCI_PORT_REG_IRQ_MASK:
        val = pr->irq_mask;
        break;
    case AHCI_PORT_REG_CMD:
        val = pr->cmd;
        break;
    case AHCI_PORT_REG_TFDATA:
        val = pr->tfdata;
        break;
    case AHCI_PORT_REG_SIG:
        val = pr->sig;
        break;
    case AHCI_PORT_REG_SCR_STAT:
        if (s->dev[port].port.ifs[0].blk) {
            val = SATA_SCR_SSTATUS_DET_DEV_PRESENT_PHY_UP |
                  SATA_SCR_SSTATUS_SPD_GEN1 | SATA_SCR_SSTATUS_IPM_ACTIVE;
        } else {
            val = SATA_SCR_SSTATUS_DET_NODEV;
        }
        break;
    case AHCI_PORT_REG_SCR_CTL:
        val = pr->scr_ctl;
        break;
    case AHCI_PORT_REG_SCR_ERR:
        val = pr->scr_err;
        break;
    case AHCI_PORT_REG_SCR_ACT:
        val = pr->scr_act;
        break;
    case AHCI_PORT_REG_CMD_ISSUE:
        val = pr->cmd_issue;
        break;
    default:
        trace_ahci_port_read_default(s, port, AHCIPortReg_lookup[regnum],
                                     offset);
        val = 0;
    }

    trace_ahci_port_read(s, port, AHCIPortReg_lookup[regnum], offset, val);
    return val;
}

static void ahci_irq_raise(AHCIState *s)
{
    DeviceState *dev_state = s->container;
    PCIDevice *pci_dev = (PCIDevice *) object_dynamic_cast(OBJECT(dev_state),
                                                           TYPE_PCI_DEVICE);

    trace_ahci_irq_raise(s);

    if (pci_dev && msi_enabled(pci_dev)) {
        msi_notify(pci_dev, 0);
    } else {
        qemu_irq_raise(s->irq);
    }
}

static void ahci_irq_lower(AHCIState *s)
{
    DeviceState *dev_state = s->container;
    PCIDevice *pci_dev = (PCIDevice *) object_dynamic_cast(OBJECT(dev_state),
                                                           TYPE_PCI_DEVICE);

    trace_ahci_irq_lower(s);

    if (!pci_dev || !msi_enabled(pci_dev)) {
        qemu_irq_lower(s->irq);
    }
}

static void ahci_check_irq(AHCIState *s)
{
    int i;
    uint32_t old_irq = s->control_regs.irqstatus;

    s->control_regs.irqstatus = 0;
    for (i = 0; i < s->ports; i++) {
        AHCIPortRegs *pr = &s->dev[i].port_regs;
        if (pr->irq_stat & pr->irq_mask) {
            s->control_regs.irqstatus |= (1 << i);
        }
    }
    trace_ahci_check_irq(s, old_irq, s->control_regs.irqstatus);
    if (s->control_regs.irqstatus &&
        (s->control_regs.ghc & HOST_CTL_IRQ_EN)) {
            ahci_irq_raise(s);
    } else {
        ahci_irq_lower(s);
    }
}

static void ahci_trigger_irq(AHCIState *s, AHCIDevice *d,
                             enum AHCIPortIRQ irqbit)
{
    g_assert((unsigned)irqbit < 32);
    uint32_t irq = 1U << irqbit;
    uint32_t irqstat = d->port_regs.irq_stat | irq;

    trace_ahci_trigger_irq(s, d->port_no,
                           AHCIPortIRQ_lookup[irqbit], irq,
                           d->port_regs.irq_stat, irqstat,
                           irqstat & d->port_regs.irq_mask);

    d->port_regs.irq_stat = irqstat;
    ahci_check_irq(s);
}

static void map_page(AddressSpace *as, uint8_t **ptr, uint64_t addr,
                     uint32_t wanted)
{
    hwaddr len = wanted;

    if (*ptr) {
        dma_memory_unmap(as, *ptr, len, DMA_DIRECTION_FROM_DEVICE, len);
    }

    *ptr = dma_memory_map(as, addr, &len, DMA_DIRECTION_FROM_DEVICE,
                          MEMTXATTRS_UNSPECIFIED);
    if (len < wanted && *ptr) {
        dma_memory_unmap(as, *ptr, len, DMA_DIRECTION_FROM_DEVICE, len);
        *ptr = NULL;
    }
}

/**
 * Check the cmd register to see if we should start or stop
 * the DMA or FIS RX engines.
 *
 * @ad: Device to dis/engage.
 *
 * @return 0 on success, -1 on error.
 */
static int ahci_cond_start_engines(AHCIDevice *ad)
{
    AHCIPortRegs *pr = &ad->port_regs;
    bool cmd_start = pr->cmd & PORT_CMD_START;
    bool cmd_on    = pr->cmd & PORT_CMD_LIST_ON;
    bool fis_start = pr->cmd & PORT_CMD_FIS_RX;
    bool fis_on    = pr->cmd & PORT_CMD_FIS_ON;

    if (cmd_start && !cmd_on) {
        if (!ahci_map_clb_address(ad)) {
            pr->cmd &= ~PORT_CMD_START;
            error_report("AHCI: Failed to start DMA engine: "
                         "bad command list buffer address");
            return -1;
        }
    } else if (!cmd_start && cmd_on) {
        ahci_unmap_clb_address(ad);
    }

    if (fis_start && !fis_on) {
        if (!ahci_map_fis_address(ad)) {
            pr->cmd &= ~PORT_CMD_FIS_RX;
            error_report("AHCI: Failed to start FIS receive engine: "
                         "bad FIS receive buffer address");
            return -1;
        }
    } else if (!fis_start && fis_on) {
        ahci_unmap_fis_address(ad);
    }

    return 0;
}

static void ahci_port_write(AHCIState *s, int port, int offset, uint32_t val)
{
    AHCIPortRegs *pr = &s->dev[port].port_regs;
    enum AHCIPortReg regnum = offset / sizeof(uint32_t);
    assert(regnum < (AHCI_PORT_ADDR_OFFSET_LEN / sizeof(uint32_t)));
    trace_ahci_port_write(s, port, AHCIPortReg_lookup[regnum], offset, val);

    switch (regnum) {
    case AHCI_PORT_REG_LST_ADDR:
        pr->lst_addr = val;
        break;
    case AHCI_PORT_REG_LST_ADDR_HI:
        pr->lst_addr_hi = val;
        break;
    case AHCI_PORT_REG_FIS_ADDR:
        pr->fis_addr = val;
        break;
    case AHCI_PORT_REG_FIS_ADDR_HI:
        pr->fis_addr_hi = val;
        break;
    case AHCI_PORT_REG_IRQ_STAT:
        pr->irq_stat &= ~val;
        ahci_check_irq(s);
        break;
    case AHCI_PORT_REG_IRQ_MASK:
        pr->irq_mask = val & 0xfdc000ff;
        ahci_check_irq(s);
        break;
    case AHCI_PORT_REG_CMD:
        /* Block any Read-only fields from being set;
         * including LIST_ON and FIS_ON.
         * The spec requires to set ICC bits to zero after the ICC change
         * is done. We don't support ICC state changes, therefore always
         * force the ICC bits to zero.
         */
        pr->cmd = (pr->cmd & PORT_CMD_RO_MASK) |
            (val & ~(PORT_CMD_RO_MASK | PORT_CMD_ICC_MASK));

        /* Check FIS RX and CLB engines */
        ahci_cond_start_engines(&s->dev[port]);

        /* XXX usually the FIS would be pending on the bus here and
           issuing deferred until the OS enables FIS receival.
           Instead, we only submit it once - which works in most
           cases, but is a hack. */
        if ((pr->cmd & PORT_CMD_FIS_ON) &&
            !s->dev[port].init_d2h_sent) {
            ahci_init_d2h(&s->dev[port]);
        }

        check_cmd(s, port);
        break;
    case AHCI_PORT_REG_TFDATA:
    case AHCI_PORT_REG_SIG:
    case AHCI_PORT_REG_SCR_STAT:
        /* Read Only */
        break;
    case AHCI_PORT_REG_SCR_CTL:
        if (((pr->scr_ctl & AHCI_SCR_SCTL_DET) == 1) &&
            ((val & AHCI_SCR_SCTL_DET) == 0)) {
            ahci_reset_port(s, port);
        }
        pr->scr_ctl = val;
        break;
    case AHCI_PORT_REG_SCR_ERR:
        pr->scr_err &= ~val;
        break;
    case AHCI_PORT_REG_SCR_ACT:
        /* RW1 */
        pr->scr_act |= val;
        break;
    case AHCI_PORT_REG_CMD_ISSUE:
        pr->cmd_issue |= val;
        check_cmd(s, port);
        break;
    default:
        trace_ahci_port_write_unimpl(s, port, AHCIPortReg_lookup[regnum],
                                     offset, val);
        qemu_log_mask(LOG_UNIMP, "Attempted write to unimplemented register: "
                      "AHCI port %d register %s, offset 0x%x: 0x%"PRIx32,
                      port, AHCIPortReg_lookup[regnum], offset, val);
        break;
    }
}

static uint64_t ahci_mem_read_32(void *opaque, hwaddr addr)
{
    AHCIState *s = opaque;
    uint32_t val = 0;

    if (addr < AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR) {
        enum AHCIHostReg regnum = addr / 4;
        assert(regnum < AHCI_HOST_REG__COUNT);

        switch (regnum) {
        case AHCI_HOST_REG_CAP:
            val = s->control_regs.cap;
            break;
        case AHCI_HOST_REG_CTL:
            val = s->control_regs.ghc;
            break;
        case AHCI_HOST_REG_IRQ_STAT:
            val = s->control_regs.irqstatus;
            break;
        case AHCI_HOST_REG_PORTS_IMPL:
            val = s->control_regs.impl;
            break;
        case AHCI_HOST_REG_VERSION:
            val = s->control_regs.version;
            break;
        default:
            trace_ahci_mem_read_32_host_default(s, AHCIHostReg_lookup[regnum],
                                                addr);
        }
        trace_ahci_mem_read_32_host(s, AHCIHostReg_lookup[regnum], addr, val);
    } else if ((addr >= AHCI_PORT_REGS_START_ADDR) &&
               (addr < (AHCI_PORT_REGS_START_ADDR +
                (s->ports * AHCI_PORT_ADDR_OFFSET_LEN)))) {
        val = ahci_port_read(s, (addr - AHCI_PORT_REGS_START_ADDR) >> 7,
                             addr & AHCI_PORT_ADDR_OFFSET_MASK);
    } else {
        trace_ahci_mem_read_32_default(s, addr, val);
    }

    trace_ahci_mem_read_32(s, addr, val);
    return val;
}


/**
 * AHCI 1.3 section 3 ("HBA Memory Registers")
 * Support unaligned 8/16/32 bit reads, and 64 bit aligned reads.
 * Caller is responsible for masking unwanted higher order bytes.
 */
static uint64_t ahci_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr aligned = addr & ~0x3;
    int ofst = addr - aligned;
    uint64_t lo = ahci_mem_read_32(opaque, aligned);
    uint64_t hi;
    uint64_t val;

    /* if < 8 byte read does not cross 4 byte boundary */
    if (ofst + size <= 4) {
        val = lo >> (ofst * 8);
    } else {
        g_assert(size > 1);

        /* If the 64bit read is unaligned, we will produce undefined
         * results. AHCI does not support unaligned 64bit reads. */
        hi = ahci_mem_read_32(opaque, aligned + 4);
        val = (hi << 32 | lo) >> (ofst * 8);
    }

    trace_ahci_mem_read(opaque, size, addr, val);
    return val;
}


static void ahci_mem_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    AHCIState *s = opaque;

    trace_ahci_mem_write(s, size, addr, val);

    /* Only aligned reads are allowed on AHCI */
    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ahci: Mis-aligned write to addr 0x%03" HWADDR_PRIX "\n",
                      addr);
        return;
    }

    if (addr < AHCI_GENERIC_HOST_CONTROL_REGS_MAX_ADDR) {
        enum AHCIHostReg regnum = addr / 4;
        assert(regnum < AHCI_HOST_REG__COUNT);

        switch (regnum) {
        case AHCI_HOST_REG_CAP: /* R/WO, RO */
            /* FIXME handle R/WO */
            break;
        case AHCI_HOST_REG_CTL: /* R/W */
            if (val & HOST_CTL_RESET) {
                ahci_reset(s);
            } else {
                s->control_regs.ghc = (val & 0x3) | HOST_CTL_AHCI_EN;
                ahci_check_irq(s);
            }
            break;
        case AHCI_HOST_REG_IRQ_STAT: /* R/WC, RO */
            s->control_regs.irqstatus &= ~val;
            ahci_check_irq(s);
            break;
        case AHCI_HOST_REG_PORTS_IMPL: /* R/WO, RO */
            /* FIXME handle R/WO */
            break;
        case AHCI_HOST_REG_VERSION: /* RO */
            /* FIXME report write? */
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "Attempted write to unimplemented register: "
                          "AHCI host register %s, "
                          "offset 0x%"PRIx64": 0x%"PRIx64,
                          AHCIHostReg_lookup[regnum], addr, val);
            trace_ahci_mem_write_host_unimpl(s, size,
                                             AHCIHostReg_lookup[regnum], addr);
        }
        trace_ahci_mem_write_host(s, size, AHCIHostReg_lookup[regnum],
                                     addr, val);
    } else if ((addr >= AHCI_PORT_REGS_START_ADDR) &&
               (addr < (AHCI_PORT_REGS_START_ADDR +
                        (s->ports * AHCI_PORT_ADDR_OFFSET_LEN)))) {
        ahci_port_write(s, (addr - AHCI_PORT_REGS_START_ADDR) >> 7,
                        addr & AHCI_PORT_ADDR_OFFSET_MASK, val);
    } else {
        qemu_log_mask(LOG_UNIMP, "Attempted write to unimplemented register: "
                      "AHCI global register at offset 0x%"PRIx64": 0x%"PRIx64,
                      addr, val);
        trace_ahci_mem_write_unimpl(s, size, addr, val);
    }
}

static const MemoryRegionOps ahci_mem_ops = {
    .read = ahci_mem_read,
    .write = ahci_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t ahci_idp_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    AHCIState *s = opaque;

    if (addr == s->idp_offset) {
        /* index register */
        return s->idp_index;
    } else if (addr == s->idp_offset + 4) {
        /* data register - do memory read at location selected by index */
        return ahci_mem_read(opaque, s->idp_index, size);
    } else {
        return 0;
    }
}

static void ahci_idp_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    AHCIState *s = opaque;

    if (addr == s->idp_offset) {
        /* index register - mask off reserved bits */
        s->idp_index = (uint32_t)val & ((AHCI_MEM_BAR_SIZE - 1) & ~3);
    } else if (addr == s->idp_offset + 4) {
        /* data register - do memory write at location selected by index */
        ahci_mem_write(opaque, s->idp_index, val, size);
    }
}

static const MemoryRegionOps ahci_idp_ops = {
    .read = ahci_idp_read,
    .write = ahci_idp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void ahci_reg_init(AHCIState *s)
{
    int i;

    s->control_regs.cap = (s->ports - 1) |
                          (AHCI_NUM_COMMAND_SLOTS << 8) |
                          (AHCI_SUPPORTED_SPEED_GEN1 << AHCI_SUPPORTED_SPEED) |
                          HOST_CAP_NCQ | HOST_CAP_AHCI | HOST_CAP_64;

    s->control_regs.impl = (1 << s->ports) - 1;

    s->control_regs.version = AHCI_VERSION_1_0;

    for (i = 0; i < s->ports; i++) {
        s->dev[i].port_state = STATE_RUN;
    }
}

static void check_cmd(AHCIState *s, int port)
{
    AHCIPortRegs *pr = &s->dev[port].port_regs;
    uint8_t slot;

    if ((pr->cmd & PORT_CMD_START) && pr->cmd_issue) {
        for (slot = 0; (slot < 32) && pr->cmd_issue; slot++) {
            if ((pr->cmd_issue & (1U << slot)) &&
                !handle_cmd(s, port, slot)) {
                pr->cmd_issue &= ~(1U << slot);
            }
        }
    }
}

static void ahci_check_cmd_bh(void *opaque)
{
    AHCIDevice *ad = opaque;

    qemu_bh_delete(ad->check_bh);
    ad->check_bh = NULL;

    check_cmd(ad->hba, ad->port_no);
}

static void ahci_init_d2h(AHCIDevice *ad)
{
    IDEState *ide_state = &ad->port.ifs[0];
    AHCIPortRegs *pr = &ad->port_regs;

    if (ad->init_d2h_sent) {
        return;
    }

    if (ahci_write_fis_d2h(ad)) {
        ad->init_d2h_sent = true;
        /* We're emulating receiving the first Reg H2D Fis from the device;
         * Update the SIG register, but otherwise proceed as normal. */
        pr->sig = ((uint32_t)ide_state->hcyl << 24) |
            (ide_state->lcyl << 16) |
            (ide_state->sector << 8) |
            (ide_state->nsector & 0xFF);
    }
}

static void ahci_set_signature(AHCIDevice *ad, uint32_t sig)
{
    IDEState *s = &ad->port.ifs[0];
    s->hcyl = sig >> 24 & 0xFF;
    s->lcyl = sig >> 16 & 0xFF;
    s->sector = sig >> 8 & 0xFF;
    s->nsector = sig & 0xFF;

    trace_ahci_set_signature(ad->hba, ad->port_no, s->nsector, s->sector,
                             s->lcyl, s->hcyl, sig);
}

static void ahci_reset_port(AHCIState *s, int port)
{
    AHCIDevice *d = &s->dev[port];
    AHCIPortRegs *pr = &d->port_regs;
    IDEState *ide_state = &d->port.ifs[0];
    int i;

    trace_ahci_reset_port(s, port);

    ide_bus_reset(&d->port);
    ide_state->ncq_queues = AHCI_MAX_CMDS;

    pr->scr_stat = 0;
    pr->scr_err = 0;
    pr->scr_act = 0;
    pr->tfdata = 0x7F;
    pr->sig = 0xFFFFFFFF;
    d->busy_slot = -1;
    d->init_d2h_sent = false;

    ide_state = &s->dev[port].port.ifs[0];
    if (!ide_state->blk) {
        return;
    }

    /* reset ncq queue */
    for (i = 0; i < AHCI_MAX_CMDS; i++) {
        NCQTransferState *ncq_tfs = &s->dev[port].ncq_tfs[i];
        ncq_tfs->halt = false;
        if (!ncq_tfs->used) {
            continue;
        }

        if (ncq_tfs->aiocb) {
            blk_aio_cancel(ncq_tfs->aiocb);
            ncq_tfs->aiocb = NULL;
        }

        /* Maybe we just finished the request thanks to blk_aio_cancel() */
        if (!ncq_tfs->used) {
            continue;
        }

        qemu_sglist_destroy(&ncq_tfs->sglist);
        ncq_tfs->used = 0;
    }

    s->dev[port].port_state = STATE_RUN;
    if (ide_state->drive_kind == IDE_CD) {
        ahci_set_signature(d, SATA_SIGNATURE_CDROM);\
        ide_state->status = SEEK_STAT | WRERR_STAT | READY_STAT;
    } else {
        ahci_set_signature(d, SATA_SIGNATURE_DISK);
        ide_state->status = SEEK_STAT | WRERR_STAT;
    }

    ide_state->error = 1;
    ahci_init_d2h(d);
}

/* Buffer pretty output based on a raw FIS structure. */
static char *ahci_pretty_buffer_fis(const uint8_t *fis, int cmd_len)
{
    int i;
    GString *s = g_string_new("FIS:");

    for (i = 0; i < cmd_len; i++) {
        if ((i & 0xf) == 0) {
            g_string_append_printf(s, "\n0x%02x: ", i);
        }
        g_string_append_printf(s, "%02x ", fis[i]);
    }
    g_string_append_c(s, '\n');

    return g_string_free(s, FALSE);
}

static bool ahci_map_fis_address(AHCIDevice *ad)
{
    AHCIPortRegs *pr = &ad->port_regs;
    map_page(ad->hba->as, &ad->res_fis,
             ((uint64_t)pr->fis_addr_hi << 32) | pr->fis_addr, 256);
    if (ad->res_fis != NULL) {
        pr->cmd |= PORT_CMD_FIS_ON;
        return true;
    }

    pr->cmd &= ~PORT_CMD_FIS_ON;
    return false;
}

static void ahci_unmap_fis_address(AHCIDevice *ad)
{
    if (ad->res_fis == NULL) {
        trace_ahci_unmap_fis_address_null(ad->hba, ad->port_no);
        return;
    }
    ad->port_regs.cmd &= ~PORT_CMD_FIS_ON;
    dma_memory_unmap(ad->hba->as, ad->res_fis, 256,
                     DMA_DIRECTION_FROM_DEVICE, 256);
    ad->res_fis = NULL;
}

static bool ahci_map_clb_address(AHCIDevice *ad)
{
    AHCIPortRegs *pr = &ad->port_regs;
    ad->cur_cmd = NULL;
    map_page(ad->hba->as, &ad->lst,
             ((uint64_t)pr->lst_addr_hi << 32) | pr->lst_addr, 1024);
    if (ad->lst != NULL) {
        pr->cmd |= PORT_CMD_LIST_ON;
        return true;
    }

    pr->cmd &= ~PORT_CMD_LIST_ON;
    return false;
}

static void ahci_unmap_clb_address(AHCIDevice *ad)
{
    if (ad->lst == NULL) {
        trace_ahci_unmap_clb_address_null(ad->hba, ad->port_no);
        return;
    }
    ad->port_regs.cmd &= ~PORT_CMD_LIST_ON;
    dma_memory_unmap(ad->hba->as, ad->lst, 1024,
                     DMA_DIRECTION_FROM_DEVICE, 1024);
    ad->lst = NULL;
}

static void ahci_write_fis_sdb(AHCIState *s, NCQTransferState *ncq_tfs)
{
    AHCIDevice *ad = ncq_tfs->drive;
    AHCIPortRegs *pr = &ad->port_regs;
    IDEState *ide_state;
    SDBFIS *sdb_fis;

    if (!ad->res_fis ||
        !(pr->cmd & PORT_CMD_FIS_RX)) {
        return;
    }

    sdb_fis = (SDBFIS *)&ad->res_fis[RES_FIS_SDBFIS];
    ide_state = &ad->port.ifs[0];

    sdb_fis->type = SATA_FIS_TYPE_SDB;
    /* Interrupt pending & Notification bit */
    sdb_fis->flags = 0x40; /* Interrupt bit, always 1 for NCQ */
    sdb_fis->status = ide_state->status & 0x77;
    sdb_fis->error = ide_state->error;
    /* update SAct field in SDB_FIS */
    sdb_fis->payload = cpu_to_le32(ad->finished);

    /* Update shadow registers (except BSY 0x80 and DRQ 0x08) */
    pr->tfdata = (ad->port.ifs[0].error << 8) |
        (ad->port.ifs[0].status & 0x77) |
        (pr->tfdata & 0x88);
    pr->scr_act &= ~ad->finished;
    ad->finished = 0;

    /* Trigger IRQ if interrupt bit is set (which currently, it always is) */
    if (sdb_fis->flags & 0x40) {
        ahci_trigger_irq(s, ad, AHCI_PORT_IRQ_BIT_SDBS);
    }
}

static void ahci_write_fis_pio(AHCIDevice *ad, uint16_t len, bool pio_fis_i)
{
    AHCIPortRegs *pr = &ad->port_regs;
    uint8_t *pio_fis;
    IDEState *s = &ad->port.ifs[0];

    if (!ad->res_fis || !(pr->cmd & PORT_CMD_FIS_RX)) {
        return;
    }

    pio_fis = &ad->res_fis[RES_FIS_PSFIS];

    pio_fis[0] = SATA_FIS_TYPE_PIO_SETUP;
    pio_fis[1] = (pio_fis_i ? (1 << 6) : 0);
    pio_fis[2] = s->status;
    pio_fis[3] = s->error;

    pio_fis[4] = s->sector;
    pio_fis[5] = s->lcyl;
    pio_fis[6] = s->hcyl;
    pio_fis[7] = s->select;
    pio_fis[8] = s->hob_sector;
    pio_fis[9] = s->hob_lcyl;
    pio_fis[10] = s->hob_hcyl;
    pio_fis[11] = 0;
    pio_fis[12] = s->nsector & 0xFF;
    pio_fis[13] = (s->nsector >> 8) & 0xFF;
    pio_fis[14] = 0;
    pio_fis[15] = s->status;
    pio_fis[16] = len & 255;
    pio_fis[17] = len >> 8;
    pio_fis[18] = 0;
    pio_fis[19] = 0;

    /* Update shadow registers: */
    pr->tfdata = (ad->port.ifs[0].error << 8) |
        ad->port.ifs[0].status;

    if (pio_fis[2] & ERR_STAT) {
        ahci_trigger_irq(ad->hba, ad, AHCI_PORT_IRQ_BIT_TFES);
    }
}

static bool ahci_write_fis_d2h(AHCIDevice *ad)
{
    AHCIPortRegs *pr = &ad->port_regs;
    uint8_t *d2h_fis;
    int i;
    IDEState *s = &ad->port.ifs[0];

    if (!ad->res_fis || !(pr->cmd & PORT_CMD_FIS_RX)) {
        return false;
    }

    d2h_fis = &ad->res_fis[RES_FIS_RFIS];

    d2h_fis[0] = SATA_FIS_TYPE_REGISTER_D2H;
    d2h_fis[1] = (1 << 6); /* interrupt bit */
    d2h_fis[2] = s->status;
    d2h_fis[3] = s->error;

    d2h_fis[4] = s->sector;
    d2h_fis[5] = s->lcyl;
    d2h_fis[6] = s->hcyl;
    d2h_fis[7] = s->select;
    d2h_fis[8] = s->hob_sector;
    d2h_fis[9] = s->hob_lcyl;
    d2h_fis[10] = s->hob_hcyl;
    d2h_fis[11] = 0;
    d2h_fis[12] = s->nsector & 0xFF;
    d2h_fis[13] = (s->nsector >> 8) & 0xFF;
    for (i = 14; i < 20; i++) {
        d2h_fis[i] = 0;
    }

    /* Update shadow registers: */
    pr->tfdata = (ad->port.ifs[0].error << 8) |
        ad->port.ifs[0].status;

    if (d2h_fis[2] & ERR_STAT) {
        ahci_trigger_irq(ad->hba, ad, AHCI_PORT_IRQ_BIT_TFES);
    }

    ahci_trigger_irq(ad->hba, ad, AHCI_PORT_IRQ_BIT_DHRS);
    return true;
}

static int prdt_tbl_entry_size(const AHCI_SG *tbl)
{
    /* flags_size is zero-based */
    return (le32_to_cpu(tbl->flags_size) & AHCI_PRDT_SIZE_MASK) + 1;
}

/**
 * Fetch entries in a guest-provided PRDT and convert it into a QEMU SGlist.
 * @ad: The AHCIDevice for whom we are building the SGList.
 * @sglist: The SGList target to add PRD entries to.
 * @cmd: The AHCI Command Header that describes where the PRDT is.
 * @limit: The remaining size of the S/ATA transaction, in bytes.
 * @offset: The number of bytes already transferred, in bytes.
 *
 * The AHCI PRDT can describe up to 256GiB. S/ATA only support transactions of
 * up to 32MiB as of ATA8-ACS3 rev 1b, assuming a 512 byte sector size. We stop
 * building the sglist from the PRDT as soon as we hit @limit bytes,
 * which is <= INT32_MAX/2GiB.
 */
static int ahci_populate_sglist(AHCIDevice *ad, QEMUSGList *sglist,
                                AHCICmdHdr *cmd, int64_t limit, uint64_t offset)
{
    uint16_t opts = le16_to_cpu(cmd->opts);
    uint16_t prdtl = le16_to_cpu(cmd->prdtl);
    uint64_t cfis_addr = le64_to_cpu(cmd->tbl_addr);
    uint64_t prdt_addr = cfis_addr + 0x80;
    dma_addr_t prdt_len = (prdtl * sizeof(AHCI_SG));
    dma_addr_t real_prdt_len = prdt_len;
    uint8_t *prdt;
    int i;
    int r = 0;
    uint64_t sum = 0;
    int off_idx = -1;
    int64_t off_pos = -1;
    int tbl_entry_size;
    IDEBus *bus = &ad->port;
    BusState *qbus = BUS(bus);

    trace_ahci_populate_sglist(ad->hba, ad->port_no);

    if (!prdtl) {
        trace_ahci_populate_sglist_no_prdtl(ad->hba, ad->port_no, opts);
        return -1;
    }

    /* map PRDT */
    if (!(prdt = dma_memory_map(ad->hba->as, prdt_addr, &prdt_len,
                                DMA_DIRECTION_TO_DEVICE,
                                MEMTXATTRS_UNSPECIFIED))){
        trace_ahci_populate_sglist_no_map(ad->hba, ad->port_no);
        return -1;
    }

    if (prdt_len < real_prdt_len) {
        trace_ahci_populate_sglist_short_map(ad->hba, ad->port_no);
        r = -1;
        goto out;
    }

    /* Get entries in the PRDT, init a qemu sglist accordingly */
    if (prdtl > 0) {
        AHCI_SG *tbl = (AHCI_SG *)prdt;
        sum = 0;
        for (i = 0; i < prdtl; i++) {
            tbl_entry_size = prdt_tbl_entry_size(&tbl[i]);
            if (offset < (sum + tbl_entry_size)) {
                off_idx = i;
                off_pos = offset - sum;
                break;
            }
            sum += tbl_entry_size;
        }
        if ((off_idx == -1) || (off_pos < 0) || (off_pos > tbl_entry_size)) {
            trace_ahci_populate_sglist_bad_offset(ad->hba, ad->port_no,
                                                  off_idx, off_pos);
            r = -1;
            goto out;
        }

        qemu_sglist_init(sglist, qbus->parent, (prdtl - off_idx),
                         ad->hba->as);
        qemu_sglist_add(sglist, le64_to_cpu(tbl[off_idx].addr) + off_pos,
                        MIN(prdt_tbl_entry_size(&tbl[off_idx]) - off_pos,
                            limit));

        for (i = off_idx + 1; i < prdtl && sglist->size < limit; i++) {
            qemu_sglist_add(sglist, le64_to_cpu(tbl[i].addr),
                            MIN(prdt_tbl_entry_size(&tbl[i]),
                                limit - sglist->size));
        }
    }

out:
    dma_memory_unmap(ad->hba->as, prdt, prdt_len,
                     DMA_DIRECTION_TO_DEVICE, prdt_len);
    return r;
}

static void ncq_err(NCQTransferState *ncq_tfs)
{
    IDEState *ide_state = &ncq_tfs->drive->port.ifs[0];

    ide_state->error = ABRT_ERR;
    ide_state->status = READY_STAT | ERR_STAT;
    ncq_tfs->drive->port_regs.scr_err |= (1 << ncq_tfs->tag);
    qemu_sglist_destroy(&ncq_tfs->sglist);
    ncq_tfs->used = 0;
}

static void ncq_finish(NCQTransferState *ncq_tfs)
{
    /* If we didn't error out, set our finished bit. Errored commands
     * do not get a bit set for the SDB FIS ACT register, nor do they
     * clear the outstanding bit in scr_act (PxSACT). */
    if (!(ncq_tfs->drive->port_regs.scr_err & (1 << ncq_tfs->tag))) {
        ncq_tfs->drive->finished |= (1 << ncq_tfs->tag);
    }

    ahci_write_fis_sdb(ncq_tfs->drive->hba, ncq_tfs);

    trace_ncq_finish(ncq_tfs->drive->hba, ncq_tfs->drive->port_no,
                     ncq_tfs->tag);

    block_acct_done(blk_get_stats(ncq_tfs->drive->port.ifs[0].blk),
                    &ncq_tfs->acct);
    qemu_sglist_destroy(&ncq_tfs->sglist);
    ncq_tfs->used = 0;
}

static void ncq_cb(void *opaque, int ret)
{
    NCQTransferState *ncq_tfs = (NCQTransferState *)opaque;
    IDEState *ide_state = &ncq_tfs->drive->port.ifs[0];

    ncq_tfs->aiocb = NULL;

    if (ret < 0) {
        bool is_read = ncq_tfs->cmd == READ_FPDMA_QUEUED;
        BlockErrorAction action = blk_get_error_action(ide_state->blk,
                                                       is_read, -ret);
        if (action == BLOCK_ERROR_ACTION_STOP) {
            ncq_tfs->halt = true;
            ide_state->bus->error_status = IDE_RETRY_HBA;
        } else if (action == BLOCK_ERROR_ACTION_REPORT) {
            ncq_err(ncq_tfs);
        }
        blk_error_action(ide_state->blk, action, is_read, -ret);
    } else {
        ide_state->status = READY_STAT | SEEK_STAT;
    }

    if (!ncq_tfs->halt) {
        ncq_finish(ncq_tfs);
    }
}

static int is_ncq(uint8_t ata_cmd)
{
    /* Based on SATA 3.2 section 13.6.3.2 */
    switch (ata_cmd) {
    case READ_FPDMA_QUEUED:
    case WRITE_FPDMA_QUEUED:
    case NCQ_NON_DATA:
    case RECEIVE_FPDMA_QUEUED:
    case SEND_FPDMA_QUEUED:
        return 1;
    default:
        return 0;
    }
}

static void execute_ncq_command(NCQTransferState *ncq_tfs)
{
    AHCIDevice *ad = ncq_tfs->drive;
    IDEState *ide_state = &ad->port.ifs[0];
    int port = ad->port_no;

    g_assert(is_ncq(ncq_tfs->cmd));
    ncq_tfs->halt = false;

    switch (ncq_tfs->cmd) {
    case READ_FPDMA_QUEUED:
        trace_execute_ncq_command_read(ad->hba, port, ncq_tfs->tag,
                                       ncq_tfs->sector_count, ncq_tfs->lba);
        dma_acct_start(ide_state->blk, &ncq_tfs->acct,
                       &ncq_tfs->sglist, BLOCK_ACCT_READ);
        ncq_tfs->aiocb = dma_blk_read(ide_state->blk, &ncq_tfs->sglist,
                                      ncq_tfs->lba << BDRV_SECTOR_BITS,
                                      BDRV_SECTOR_SIZE,
                                      ncq_cb, ncq_tfs);
        break;
    case WRITE_FPDMA_QUEUED:
        trace_execute_ncq_command_write(ad->hba, port, ncq_tfs->tag,
                                        ncq_tfs->sector_count, ncq_tfs->lba);
        dma_acct_start(ide_state->blk, &ncq_tfs->acct,
                       &ncq_tfs->sglist, BLOCK_ACCT_WRITE);
        ncq_tfs->aiocb = dma_blk_write(ide_state->blk, &ncq_tfs->sglist,
                                       ncq_tfs->lba << BDRV_SECTOR_BITS,
                                       BDRV_SECTOR_SIZE,
                                       ncq_cb, ncq_tfs);
        break;
    default:
        trace_execute_ncq_command_unsup(ad->hba, port,
                                        ncq_tfs->tag, ncq_tfs->cmd);
        ncq_err(ncq_tfs);
    }
}


static void process_ncq_command(AHCIState *s, int port, const uint8_t *cmd_fis,
                                uint8_t slot)
{
    AHCIDevice *ad = &s->dev[port];
    const NCQFrame *ncq_fis = (NCQFrame *)cmd_fis;
    uint8_t tag = ncq_fis->tag >> 3;
    NCQTransferState *ncq_tfs = &ad->ncq_tfs[tag];
    size_t size;

    g_assert(is_ncq(ncq_fis->command));
    if (ncq_tfs->used) {
        /* error - already in use */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: tag %d already used\n",
                      __func__, tag);
        return;
    }

    ncq_tfs->used = 1;
    ncq_tfs->drive = ad;
    ncq_tfs->slot = slot;
    ncq_tfs->cmdh = &((AHCICmdHdr *)ad->lst)[slot];
    ncq_tfs->cmd = ncq_fis->command;
    ncq_tfs->lba = ((uint64_t)ncq_fis->lba5 << 40) |
                   ((uint64_t)ncq_fis->lba4 << 32) |
                   ((uint64_t)ncq_fis->lba3 << 24) |
                   ((uint64_t)ncq_fis->lba2 << 16) |
                   ((uint64_t)ncq_fis->lba1 << 8) |
                   (uint64_t)ncq_fis->lba0;
    ncq_tfs->tag = tag;

    /* Sanity-check the NCQ packet */
    if (tag != slot) {
        trace_process_ncq_command_mismatch(s, port, tag, slot);
    }

    if (ncq_fis->aux0 || ncq_fis->aux1 || ncq_fis->aux2 || ncq_fis->aux3) {
        trace_process_ncq_command_aux(s, port, tag);
    }
    if (ncq_fis->prio || ncq_fis->icc) {
        trace_process_ncq_command_prioicc(s, port, tag);
    }
    if (ncq_fis->fua & NCQ_FIS_FUA_MASK) {
        trace_process_ncq_command_fua(s, port, tag);
    }
    if (ncq_fis->tag & NCQ_FIS_RARC_MASK) {
        trace_process_ncq_command_rarc(s, port, tag);
    }

    ncq_tfs->sector_count = ((ncq_fis->sector_count_high << 8) |
                             ncq_fis->sector_count_low);
    if (!ncq_tfs->sector_count) {
        ncq_tfs->sector_count = 0x10000;
    }
    size = ncq_tfs->sector_count * BDRV_SECTOR_SIZE;
    ahci_populate_sglist(ad, &ncq_tfs->sglist, ncq_tfs->cmdh, size, 0);

    if (ncq_tfs->sglist.size < size) {
        error_report("ahci: PRDT length for NCQ command (0x" DMA_ADDR_FMT ") "
                     "is smaller than the requested size (0x%zx)",
                     ncq_tfs->sglist.size, size);
        ncq_err(ncq_tfs);
        ahci_trigger_irq(ad->hba, ad, AHCI_PORT_IRQ_BIT_OFS);
        return;
    } else if (ncq_tfs->sglist.size != size) {
        trace_process_ncq_command_large(s, port, tag,
                                        ncq_tfs->sglist.size, size);
    }

    trace_process_ncq_command(s, port, tag,
                              ncq_fis->command,
                              ncq_tfs->lba,
                              ncq_tfs->lba + ncq_tfs->sector_count - 1);
    execute_ncq_command(ncq_tfs);
}

static AHCICmdHdr *get_cmd_header(AHCIState *s, uint8_t port, uint8_t slot)
{
    if (port >= s->ports || slot >= AHCI_MAX_CMDS) {
        return NULL;
    }

    return s->dev[port].lst ? &((AHCICmdHdr *)s->dev[port].lst)[slot] : NULL;
}

static void handle_reg_h2d_fis(AHCIState *s, int port,
                               uint8_t slot, const uint8_t *cmd_fis)
{
    IDEState *ide_state = &s->dev[port].port.ifs[0];
    AHCICmdHdr *cmd = get_cmd_header(s, port, slot);
    uint16_t opts = le16_to_cpu(cmd->opts);

    if (cmd_fis[1] & 0x0F) {
        trace_handle_reg_h2d_fis_pmp(s, port, cmd_fis[1],
                                     cmd_fis[2], cmd_fis[3]);
        return;
    }

    if (cmd_fis[1] & 0x70) {
        trace_handle_reg_h2d_fis_res(s, port, cmd_fis[1],
                                     cmd_fis[2], cmd_fis[3]);
        return;
    }

    if (!(cmd_fis[1] & SATA_FIS_REG_H2D_UPDATE_COMMAND_REGISTER)) {
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
        return;
    }

    /* Check for NCQ command */
    if (is_ncq(cmd_fis[2])) {
        process_ncq_command(s, port, cmd_fis, slot);
        return;
    }

    /* Decompose the FIS:
     * AHCI does not interpret FIS packets, it only forwards them.
     * SATA 1.0 describes how to decode LBA28 and CHS FIS packets.
     * Later specifications, e.g, SATA 3.2, describe LBA48 FIS packets.
     *
     * ATA4 describes sector number for LBA28/CHS commands.
     * ATA6 describes sector number for LBA48 commands.
     * ATA8 deprecates CHS fully, describing only LBA28/48.
     *
     * We dutifully convert the FIS into IDE registers, and allow the
     * core layer to interpret them as needed. */
    ide_state->feature = cmd_fis[3];
    ide_state->sector = cmd_fis[4];      /* LBA 7:0 */
    ide_state->lcyl = cmd_fis[5];        /* LBA 15:8  */
    ide_state->hcyl = cmd_fis[6];        /* LBA 23:16 */
    ide_state->select = cmd_fis[7];      /* LBA 27:24 (LBA28) */
    ide_state->hob_sector = cmd_fis[8];  /* LBA 31:24 */
    ide_state->hob_lcyl = cmd_fis[9];    /* LBA 39:32 */
    ide_state->hob_hcyl = cmd_fis[10];   /* LBA 47:40 */
    ide_state->hob_feature = cmd_fis[11];
    ide_state->nsector = (int64_t)((cmd_fis[13] << 8) | cmd_fis[12]);
    /* 14, 16, 17, 18, 19: Reserved (SATA 1.0) */
    /* 15: Only valid when UPDATE_COMMAND not set. */

    /* Copy the ACMD field (ATAPI packet, if any) from the AHCI command
     * table to ide_state->io_buffer */
    if (opts & AHCI_CMD_ATAPI) {
        memcpy(ide_state->io_buffer, &cmd_fis[AHCI_COMMAND_TABLE_ACMD], 0x10);
        if (trace_event_get_state_backends(TRACE_HANDLE_REG_H2D_FIS_DUMP)) {
            char *pretty_fis = ahci_pretty_buffer_fis(ide_state->io_buffer, 0x10);
            trace_handle_reg_h2d_fis_dump(s, port, pretty_fis);
            g_free(pretty_fis);
        }
    }

    ide_state->error = 0;
    s->dev[port].done_first_drq = false;
    /* Reset transferred byte counter */
    cmd->status = 0;

    /* We're ready to process the command in FIS byte 2. */
    ide_bus_exec_cmd(&s->dev[port].port, cmd_fis[2]);
}

static int handle_cmd(AHCIState *s, int port, uint8_t slot)
{
    IDEState *ide_state;
    uint64_t tbl_addr;
    AHCICmdHdr *cmd;
    uint8_t *cmd_fis;
    dma_addr_t cmd_len;

    if (s->dev[port].port.ifs[0].status & (BUSY_STAT|DRQ_STAT)) {
        /* Engine currently busy, try again later */
        trace_handle_cmd_busy(s, port);
        return -1;
    }

    if (!s->dev[port].lst) {
        trace_handle_cmd_nolist(s, port);
        return -1;
    }
    cmd = get_cmd_header(s, port, slot);
    /* remember current slot handle for later */
    s->dev[port].cur_cmd = cmd;

    /* The device we are working for */
    ide_state = &s->dev[port].port.ifs[0];
    if (!ide_state->blk) {
        trace_handle_cmd_badport(s, port);
        return -1;
    }

    tbl_addr = le64_to_cpu(cmd->tbl_addr);
    cmd_len = 0x80;
    cmd_fis = dma_memory_map(s->as, tbl_addr, &cmd_len,
                             DMA_DIRECTION_TO_DEVICE, MEMTXATTRS_UNSPECIFIED);
    if (!cmd_fis) {
        trace_handle_cmd_badfis(s, port);
        return -1;
    } else if (cmd_len != 0x80) {
        ahci_trigger_irq(s, &s->dev[port], AHCI_PORT_IRQ_BIT_HBFS);
        trace_handle_cmd_badmap(s, port, cmd_len);
        goto out;
    }
    if (trace_event_get_state_backends(TRACE_HANDLE_CMD_FIS_DUMP)) {
        char *pretty_fis = ahci_pretty_buffer_fis(cmd_fis, 0x80);
        trace_handle_cmd_fis_dump(s, port, pretty_fis);
        g_free(pretty_fis);
    }
    switch (cmd_fis[0]) {
        case SATA_FIS_TYPE_REGISTER_H2D:
            handle_reg_h2d_fis(s, port, slot, cmd_fis);
            break;
        default:
            trace_handle_cmd_unhandled_fis(s, port,
                                           cmd_fis[0], cmd_fis[1], cmd_fis[2]);
            break;
    }

out:
    dma_memory_unmap(s->as, cmd_fis, cmd_len, DMA_DIRECTION_TO_DEVICE,
                     cmd_len);

    if (s->dev[port].port.ifs[0].status & (BUSY_STAT|DRQ_STAT)) {
        /* async command, complete later */
        s->dev[port].busy_slot = slot;
        return -1;
    }

    /* done handling the command */
    return 0;
}

/* Transfer PIO data between RAM and device */
static void ahci_pio_transfer(const IDEDMA *dma)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    IDEState *s = &ad->port.ifs[0];
    uint32_t size = (uint32_t)(s->data_end - s->data_ptr);
    /* write == ram -> device */
    uint16_t opts = le16_to_cpu(ad->cur_cmd->opts);
    int is_write = opts & AHCI_CMD_WRITE;
    int is_atapi = opts & AHCI_CMD_ATAPI;
    int has_sglist = 0;
    bool pio_fis_i;

    /* The PIO Setup FIS is received prior to transfer, but the interrupt
     * is only triggered after data is received.
     *
     * The device only sets the 'I' bit in the PIO Setup FIS for device->host
     * requests (see "DPIOI1" in the SATA spec), or for host->device DRQs after
     * the first (see "DPIOO1").  The latter is consistent with the spec's
     * description of the PACKET protocol, where the command part of ATAPI requests
     * ("DPKT0") has the 'I' bit clear, while the data part of PIO ATAPI requests
     * ("DPKT4a" and "DPKT7") has the 'I' bit set for both directions for all DRQs.
     */
    pio_fis_i = ad->done_first_drq || (!is_atapi && !is_write);
    ahci_write_fis_pio(ad, size, pio_fis_i);

    if (is_atapi && !ad->done_first_drq) {
        /* already prepopulated iobuffer */
        goto out;
    }

    if (ahci_dma_prepare_buf(dma, size)) {
        has_sglist = 1;
    }

    trace_ahci_pio_transfer(ad->hba, ad->port_no, is_write ? "writ" : "read",
                            size, is_atapi ? "atapi" : "ata",
                            has_sglist ? "" : "o");

    if (has_sglist && size) {
        const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

        if (is_write) {
            dma_buf_write(s->data_ptr, size, NULL, &s->sg, attrs);
        } else {
            dma_buf_read(s->data_ptr, size, NULL, &s->sg, attrs);
        }
    }

    /* Update number of transferred bytes, destroy sglist */
    dma_buf_commit(s, size);

out:
    /* declare that we processed everything */
    s->data_ptr = s->data_end;

    ad->done_first_drq = true;
    if (pio_fis_i) {
        ahci_trigger_irq(ad->hba, ad, AHCI_PORT_IRQ_BIT_PSS);
    }
}

static void ahci_start_dma(const IDEDMA *dma, IDEState *s,
                           BlockCompletionFunc *dma_cb)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    trace_ahci_start_dma(ad->hba, ad->port_no);
    s->io_buffer_offset = 0;
    dma_cb(s, 0);
}

static void ahci_restart_dma(const IDEDMA *dma)
{
    /* Nothing to do, ahci_start_dma already resets s->io_buffer_offset.  */
}

/**
 * IDE/PIO restarts are handled by the core layer, but NCQ commands
 * need an extra kick from the AHCI HBA.
 */
static void ahci_restart(const IDEDMA *dma)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    int i;

    for (i = 0; i < AHCI_MAX_CMDS; i++) {
        NCQTransferState *ncq_tfs = &ad->ncq_tfs[i];
        if (ncq_tfs->halt) {
            execute_ncq_command(ncq_tfs);
        }
    }
}

/**
 * Called in DMA and PIO R/W chains to read the PRDT.
 * Not shared with NCQ pathways.
 */
static int32_t ahci_dma_prepare_buf(const IDEDMA *dma, int32_t limit)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    IDEState *s = &ad->port.ifs[0];

    if (ahci_populate_sglist(ad, &s->sg, ad->cur_cmd,
                             limit, s->io_buffer_offset) == -1) {
        trace_ahci_dma_prepare_buf_fail(ad->hba, ad->port_no);
        return -1;
    }
    s->io_buffer_size = s->sg.size;

    trace_ahci_dma_prepare_buf(ad->hba, ad->port_no, limit, s->io_buffer_size);
    return s->io_buffer_size;
}

/**
 * Updates the command header with a bytes-read value.
 * Called via dma_buf_commit, for both DMA and PIO paths.
 * sglist destruction is handled within dma_buf_commit.
 */
static void ahci_commit_buf(const IDEDMA *dma, uint32_t tx_bytes)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);

    tx_bytes += le32_to_cpu(ad->cur_cmd->status);
    ad->cur_cmd->status = cpu_to_le32(tx_bytes);
}

static int ahci_dma_rw_buf(const IDEDMA *dma, bool is_write)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);
    IDEState *s = &ad->port.ifs[0];
    uint8_t *p = s->io_buffer + s->io_buffer_index;
    int l = s->io_buffer_size - s->io_buffer_index;

    if (ahci_populate_sglist(ad, &s->sg, ad->cur_cmd, l, s->io_buffer_offset)) {
        return 0;
    }

    if (is_write) {
        dma_buf_read(p, l, NULL, &s->sg, MEMTXATTRS_UNSPECIFIED);
    } else {
        dma_buf_write(p, l, NULL, &s->sg, MEMTXATTRS_UNSPECIFIED);
    }

    /* free sglist, update byte count */
    dma_buf_commit(s, l);
    s->io_buffer_index += l;

    trace_ahci_dma_rw_buf(ad->hba, ad->port_no, l);
    return 1;
}

static void ahci_cmd_done(const IDEDMA *dma)
{
    AHCIDevice *ad = DO_UPCAST(AHCIDevice, dma, dma);

    trace_ahci_cmd_done(ad->hba, ad->port_no);

    /* no longer busy */
    if (ad->busy_slot != -1) {
        ad->port_regs.cmd_issue &= ~(1 << ad->busy_slot);
        ad->busy_slot = -1;
    }

    /* update d2h status */
    ahci_write_fis_d2h(ad);

    if (ad->port_regs.cmd_issue && !ad->check_bh) {
        ad->check_bh = qemu_bh_new_guarded(ahci_check_cmd_bh, ad,
                                           &ad->mem_reentrancy_guard);
        qemu_bh_schedule(ad->check_bh);
    }
}

static void ahci_irq_set(void *opaque, int n, int level)
{
    qemu_log_mask(LOG_UNIMP, "ahci: IRQ#%d level:%d\n", n, level);
}

static const IDEDMAOps ahci_dma_ops = {
    .start_dma = ahci_start_dma,
    .restart = ahci_restart,
    .restart_dma = ahci_restart_dma,
    .pio_transfer = ahci_pio_transfer,
    .prepare_buf = ahci_dma_prepare_buf,
    .commit_buf = ahci_commit_buf,
    .rw_buf = ahci_dma_rw_buf,
    .cmd_done = ahci_cmd_done,
};

void ahci_init(AHCIState *s, DeviceState *qdev)
{
    s->container = qdev;
    /* XXX BAR size should be 1k, but that breaks, so bump it to 4k for now */
    memory_region_init_io(&s->mem, OBJECT(qdev), &ahci_mem_ops, s,
                          "ahci", AHCI_MEM_BAR_SIZE);
    memory_region_init_io(&s->idp, OBJECT(qdev), &ahci_idp_ops, s,
                          "ahci-idp", 32);
}

void ahci_realize(AHCIState *s, DeviceState *qdev, AddressSpace *as, int ports)
{
    qemu_irq *irqs;
    int i;

    s->as = as;
    s->ports = ports;
    s->dev = g_new0(AHCIDevice, ports);
    ahci_reg_init(s);
    irqs = qemu_allocate_irqs(ahci_irq_set, s, s->ports);
    for (i = 0; i < s->ports; i++) {
        AHCIDevice *ad = &s->dev[i];

        ide_bus_init(&ad->port, sizeof(ad->port), qdev, i, 1);
        ide_bus_init_output_irq(&ad->port, irqs[i]);

        ad->hba = s;
        ad->port_no = i;
        ad->port.dma = &ad->dma;
        ad->port.dma->ops = &ahci_dma_ops;
        ide_bus_register_restart_cb(&ad->port);
    }
    g_free(irqs);
}

void ahci_uninit(AHCIState *s)
{
    int i, j;

    for (i = 0; i < s->ports; i++) {
        AHCIDevice *ad = &s->dev[i];

        for (j = 0; j < 2; j++) {
            IDEState *s = &ad->port.ifs[j];

            ide_exit(s);
        }
        object_unparent(OBJECT(&ad->port));
    }

    g_free(s->dev);
}

void ahci_reset(AHCIState *s)
{
    AHCIPortRegs *pr;
    int i;

    trace_ahci_reset(s);

    s->control_regs.irqstatus = 0;
    /* AHCI Enable (AE)
     * The implementation of this bit is dependent upon the value of the
     * CAP.SAM bit. If CAP.SAM is '0', then GHC.AE shall be read-write and
     * shall have a reset value of '0'. If CAP.SAM is '1', then AE shall be
     * read-only and shall have a reset value of '1'.
     *
     * We set HOST_CAP_AHCI so we must enable AHCI at reset.
     */
    s->control_regs.ghc = HOST_CTL_AHCI_EN;

    for (i = 0; i < s->ports; i++) {
        pr = &s->dev[i].port_regs;
        pr->irq_stat = 0;
        pr->irq_mask = 0;
        pr->scr_ctl = 0;
        pr->cmd = PORT_CMD_SPIN_UP | PORT_CMD_POWER_ON;
        ahci_reset_port(s, i);
    }
}

static const VMStateDescription vmstate_ncq_tfs = {
    .name = "ncq state",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sector_count, NCQTransferState),
        VMSTATE_UINT64(lba, NCQTransferState),
        VMSTATE_UINT8(tag, NCQTransferState),
        VMSTATE_UINT8(cmd, NCQTransferState),
        VMSTATE_UINT8(slot, NCQTransferState),
        VMSTATE_BOOL(used, NCQTransferState),
        VMSTATE_BOOL(halt, NCQTransferState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_ahci_device = {
    .name = "ahci port",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_IDE_BUS(port, AHCIDevice),
        VMSTATE_IDE_DRIVE(port.ifs[0], AHCIDevice),
        VMSTATE_UINT32(port_state, AHCIDevice),
        VMSTATE_UINT32(finished, AHCIDevice),
        VMSTATE_UINT32(port_regs.lst_addr, AHCIDevice),
        VMSTATE_UINT32(port_regs.lst_addr_hi, AHCIDevice),
        VMSTATE_UINT32(port_regs.fis_addr, AHCIDevice),
        VMSTATE_UINT32(port_regs.fis_addr_hi, AHCIDevice),
        VMSTATE_UINT32(port_regs.irq_stat, AHCIDevice),
        VMSTATE_UINT32(port_regs.irq_mask, AHCIDevice),
        VMSTATE_UINT32(port_regs.cmd, AHCIDevice),
        VMSTATE_UINT32(port_regs.tfdata, AHCIDevice),
        VMSTATE_UINT32(port_regs.sig, AHCIDevice),
        VMSTATE_UINT32(port_regs.scr_stat, AHCIDevice),
        VMSTATE_UINT32(port_regs.scr_ctl, AHCIDevice),
        VMSTATE_UINT32(port_regs.scr_err, AHCIDevice),
        VMSTATE_UINT32(port_regs.scr_act, AHCIDevice),
        VMSTATE_UINT32(port_regs.cmd_issue, AHCIDevice),
        VMSTATE_BOOL(done_first_drq, AHCIDevice),
        VMSTATE_INT32(busy_slot, AHCIDevice),
        VMSTATE_BOOL(init_d2h_sent, AHCIDevice),
        VMSTATE_STRUCT_ARRAY(ncq_tfs, AHCIDevice, AHCI_MAX_CMDS,
                             1, vmstate_ncq_tfs, NCQTransferState),
        VMSTATE_END_OF_LIST()
    },
};

static int ahci_state_post_load(void *opaque, int version_id)
{
    int i, j;
    struct AHCIDevice *ad;
    NCQTransferState *ncq_tfs;
    AHCIPortRegs *pr;
    AHCIState *s = opaque;

    for (i = 0; i < s->ports; i++) {
        ad = &s->dev[i];
        pr = &ad->port_regs;

        if (!(pr->cmd & PORT_CMD_START) && (pr->cmd & PORT_CMD_LIST_ON)) {
            error_report("AHCI: DMA engine should be off, but status bit "
                         "indicates it is still running.");
            return -1;
        }
        if (!(pr->cmd & PORT_CMD_FIS_RX) && (pr->cmd & PORT_CMD_FIS_ON)) {
            error_report("AHCI: FIS RX engine should be off, but status bit "
                         "indicates it is still running.");
            return -1;
        }

        /* After a migrate, the DMA/FIS engines are "off" and
         * need to be conditionally restarted */
        pr->cmd &= ~(PORT_CMD_LIST_ON | PORT_CMD_FIS_ON);
        if (ahci_cond_start_engines(ad) != 0) {
            return -1;
        }

        for (j = 0; j < AHCI_MAX_CMDS; j++) {
            ncq_tfs = &ad->ncq_tfs[j];
            ncq_tfs->drive = ad;

            if (ncq_tfs->used != ncq_tfs->halt) {
                return -1;
            }
            if (!ncq_tfs->halt) {
                continue;
            }
            if (!is_ncq(ncq_tfs->cmd)) {
                return -1;
            }
            if (ncq_tfs->slot != ncq_tfs->tag) {
                return -1;
            }
            /* If ncq_tfs->halt is justly set, the engine should be engaged,
             * and the command list buffer should be mapped. */
            ncq_tfs->cmdh = get_cmd_header(s, i, ncq_tfs->slot);
            if (!ncq_tfs->cmdh) {
                return -1;
            }
            ahci_populate_sglist(ncq_tfs->drive, &ncq_tfs->sglist,
                                 ncq_tfs->cmdh,
                                 ncq_tfs->sector_count * BDRV_SECTOR_SIZE,
                                 0);
            if (ncq_tfs->sector_count != ncq_tfs->sglist.size >> 9) {
                return -1;
            }
        }


        /*
         * If an error is present, ad->busy_slot will be valid and not -1.
         * In this case, an operation is waiting to resume and will re-check
         * for additional AHCI commands to execute upon completion.
         *
         * In the case where no error was present, busy_slot will be -1,
         * and we should check to see if there are additional commands waiting.
         */
        if (ad->busy_slot == -1) {
            check_cmd(s, i);
        } else {
            /* We are in the middle of a command, and may need to access
             * the command header in guest memory again. */
            if (ad->busy_slot < 0 || ad->busy_slot >= AHCI_MAX_CMDS) {
                return -1;
            }
            ad->cur_cmd = get_cmd_header(s, i, ad->busy_slot);
        }
    }

    return 0;
}

const VMStateDescription vmstate_ahci = {
    .name = "ahci",
    .version_id = 1,
    .post_load = ahci_state_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(dev, AHCIState, ports,
                                     vmstate_ahci_device, AHCIDevice),
        VMSTATE_UINT32(control_regs.cap, AHCIState),
        VMSTATE_UINT32(control_regs.ghc, AHCIState),
        VMSTATE_UINT32(control_regs.irqstatus, AHCIState),
        VMSTATE_UINT32(control_regs.impl, AHCIState),
        VMSTATE_UINT32(control_regs.version, AHCIState),
        VMSTATE_UINT32(idp_index, AHCIState),
        VMSTATE_INT32_EQUAL(ports, AHCIState, NULL),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_sysbus_ahci = {
    .name = "sysbus-ahci",
    .fields = (VMStateField[]) {
        VMSTATE_AHCI(ahci, SysbusAHCIState),
        VMSTATE_END_OF_LIST()
    },
};

static void sysbus_ahci_reset(DeviceState *dev)
{
    SysbusAHCIState *s = SYSBUS_AHCI(dev);

    ahci_reset(&s->ahci);
}

static void sysbus_ahci_init(Object *obj)
{
    SysbusAHCIState *s = SYSBUS_AHCI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    ahci_init(&s->ahci, DEVICE(obj));

    sysbus_init_mmio(sbd, &s->ahci.mem);
    sysbus_init_irq(sbd, &s->ahci.irq);
}

static void sysbus_ahci_realize(DeviceState *dev, Error **errp)
{
    SysbusAHCIState *s = SYSBUS_AHCI(dev);

    ahci_realize(&s->ahci, dev, &address_space_memory, s->num_ports);
}

static Property sysbus_ahci_properties[] = {
    DEFINE_PROP_UINT32("num-ports", SysbusAHCIState, num_ports, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void sysbus_ahci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sysbus_ahci_realize;
    dc->vmsd = &vmstate_sysbus_ahci;
    device_class_set_props(dc, sysbus_ahci_properties);
    dc->reset = sysbus_ahci_reset;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo sysbus_ahci_info = {
    .name          = TYPE_SYSBUS_AHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysbusAHCIState),
    .instance_init = sysbus_ahci_init,
    .class_init    = sysbus_ahci_class_init,
};

static void sysbus_ahci_register_types(void)
{
    type_register_static(&sysbus_ahci_info);
}

type_init(sysbus_ahci_register_types)

int32_t ahci_get_num_ports(PCIDevice *dev)
{
    AHCIPCIState *d = ICH9_AHCI(dev);
    AHCIState *ahci = &d->ahci;

    return ahci->ports;
}

void ahci_ide_create_devs(PCIDevice *dev, DriveInfo **hd)
{
    AHCIPCIState *d = ICH9_AHCI(dev);
    AHCIState *ahci = &d->ahci;
    int i;

    for (i = 0; i < ahci->ports; i++) {
        if (hd[i] == NULL) {
            continue;
        }
        ide_bus_create_drive(&ahci->dev[i].port, 0, hd[i]);
    }

}
