/*
 * QEMU emulation of AMD IOMMU (AMD-Vi)
 *
 * Copyright (C) 2011 Eduard - Gabriel Munteanu
 * Copyright (C) 2015, 2016 David Kiarie Kahurani
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Cache implementation inspired by hw/i386/intel_iommu.c
 */

#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "amd_iommu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/i386/apic_internal.h"
#include "trace.h"
#include "hw/i386/apic-msidef.h"

/* used AMD-Vi MMIO registers */
const char *amdvi_mmio_low[] = {
    "AMDVI_MMIO_DEVTAB_BASE",
    "AMDVI_MMIO_CMDBUF_BASE",
    "AMDVI_MMIO_EVTLOG_BASE",
    "AMDVI_MMIO_CONTROL",
    "AMDVI_MMIO_EXCL_BASE",
    "AMDVI_MMIO_EXCL_LIMIT",
    "AMDVI_MMIO_EXT_FEATURES",
    "AMDVI_MMIO_PPR_BASE",
    "UNHANDLED"
};
const char *amdvi_mmio_high[] = {
    "AMDVI_MMIO_COMMAND_HEAD",
    "AMDVI_MMIO_COMMAND_TAIL",
    "AMDVI_MMIO_EVTLOG_HEAD",
    "AMDVI_MMIO_EVTLOG_TAIL",
    "AMDVI_MMIO_STATUS",
    "AMDVI_MMIO_PPR_HEAD",
    "AMDVI_MMIO_PPR_TAIL",
    "UNHANDLED"
};

struct AMDVIAddressSpace {
    uint8_t bus_num;            /* bus number                           */
    uint8_t devfn;              /* device function                      */
    AMDVIState *iommu_state;    /* AMDVI - one per machine              */
    MemoryRegion root;          /* AMDVI Root memory map region */
    IOMMUMemoryRegion iommu;    /* Device's address translation region  */
    MemoryRegion iommu_ir;      /* Device's interrupt remapping region  */
    AddressSpace as;            /* device's corresponding address space */
};

/* AMDVI cache entry */
typedef struct AMDVIIOTLBEntry {
    uint16_t domid;             /* assigned domain id  */
    uint16_t devid;             /* device owning entry */
    uint64_t perms;             /* access permissions  */
    uint64_t translated_addr;   /* translated address  */
    uint64_t page_mask;         /* physical page size  */
} AMDVIIOTLBEntry;

/* configure MMIO registers at startup/reset */
static void amdvi_set_quad(AMDVIState *s, hwaddr addr, uint64_t val,
                           uint64_t romask, uint64_t w1cmask)
{
    stq_le_p(&s->mmior[addr], val);
    stq_le_p(&s->romask[addr], romask);
    stq_le_p(&s->w1cmask[addr], w1cmask);
}

static uint16_t amdvi_readw(AMDVIState *s, hwaddr addr)
{
    return lduw_le_p(&s->mmior[addr]);
}

static uint32_t amdvi_readl(AMDVIState *s, hwaddr addr)
{
    return ldl_le_p(&s->mmior[addr]);
}

static uint64_t amdvi_readq(AMDVIState *s, hwaddr addr)
{
    return ldq_le_p(&s->mmior[addr]);
}

/* internal write */
static void amdvi_writeq_raw(AMDVIState *s, uint64_t val, hwaddr addr)
{
    stq_le_p(&s->mmior[addr], val);
}

/* external write */
static void amdvi_writew(AMDVIState *s, hwaddr addr, uint16_t val)
{
    uint16_t romask = lduw_le_p(&s->romask[addr]);
    uint16_t w1cmask = lduw_le_p(&s->w1cmask[addr]);
    uint16_t oldval = lduw_le_p(&s->mmior[addr]);
    stw_le_p(&s->mmior[addr],
            ((oldval & romask) | (val & ~romask)) & ~(val & w1cmask));
}

static void amdvi_writel(AMDVIState *s, hwaddr addr, uint32_t val)
{
    uint32_t romask = ldl_le_p(&s->romask[addr]);
    uint32_t w1cmask = ldl_le_p(&s->w1cmask[addr]);
    uint32_t oldval = ldl_le_p(&s->mmior[addr]);
    stl_le_p(&s->mmior[addr],
            ((oldval & romask) | (val & ~romask)) & ~(val & w1cmask));
}

static void amdvi_writeq(AMDVIState *s, hwaddr addr, uint64_t val)
{
    uint64_t romask = ldq_le_p(&s->romask[addr]);
    uint64_t w1cmask = ldq_le_p(&s->w1cmask[addr]);
    uint32_t oldval = ldq_le_p(&s->mmior[addr]);
    stq_le_p(&s->mmior[addr],
            ((oldval & romask) | (val & ~romask)) & ~(val & w1cmask));
}

/* OR a 64-bit register with a 64-bit value */
static bool amdvi_test_mask(AMDVIState *s, hwaddr addr, uint64_t val)
{
    return amdvi_readq(s, addr) | val;
}

/* OR a 64-bit register with a 64-bit value storing result in the register */
static void amdvi_assign_orq(AMDVIState *s, hwaddr addr, uint64_t val)
{
    amdvi_writeq_raw(s, addr, amdvi_readq(s, addr) | val);
}

/* AND a 64-bit register with a 64-bit value storing result in the register */
static void amdvi_assign_andq(AMDVIState *s, hwaddr addr, uint64_t val)
{
   amdvi_writeq_raw(s, addr, amdvi_readq(s, addr) & val);
}

static void amdvi_generate_msi_interrupt(AMDVIState *s)
{
    MSIMessage msg = {};
    MemTxAttrs attrs = {
        .requester_id = pci_requester_id(&s->pci.dev)
    };

    if (msi_enabled(&s->pci.dev)) {
        msg = msi_get_message(&s->pci.dev, 0);
        address_space_stl_le(&address_space_memory, msg.address, msg.data,
                             attrs, NULL);
    }
}

static void amdvi_log_event(AMDVIState *s, uint64_t *evt)
{
    /* event logging not enabled */
    if (!s->evtlog_enabled || amdvi_test_mask(s, AMDVI_MMIO_STATUS,
        AMDVI_MMIO_STATUS_EVT_OVF)) {
        return;
    }

    /* event log buffer full */
    if (s->evtlog_tail >= s->evtlog_len) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_EVT_OVF);
        /* generate interrupt */
        amdvi_generate_msi_interrupt(s);
        return;
    }

    if (dma_memory_write(&address_space_memory, s->evtlog + s->evtlog_tail,
                         evt, AMDVI_EVENT_LEN)) {
        trace_amdvi_evntlog_fail(s->evtlog, s->evtlog_tail);
    }

    s->evtlog_tail += AMDVI_EVENT_LEN;
    amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_COMP_INT);
    amdvi_generate_msi_interrupt(s);
}

static void amdvi_setevent_bits(uint64_t *buffer, uint64_t value, int start,
                                int length)
{
    int index = start / 64, bitpos = start % 64;
    uint64_t mask = MAKE_64BIT_MASK(start, length);
    buffer[index] &= ~mask;
    buffer[index] |= (value << bitpos) & mask;
}
/*
 * AMDVi event structure
 *    0:15   -> DeviceID
 *    55:63  -> event type + miscellaneous info
 *    63:127 -> related address
 */
static void amdvi_encode_event(uint64_t *evt, uint16_t devid, uint64_t addr,
                               uint16_t info)
{
    amdvi_setevent_bits(evt, devid, 0, 16);
    amdvi_setevent_bits(evt, info, 55, 8);
    amdvi_setevent_bits(evt, addr, 63, 64);
}
/* log an error encountered during a page walk
 *
 * @addr: virtual address in translation request
 */
static void amdvi_page_fault(AMDVIState *s, uint16_t devid,
                             hwaddr addr, uint16_t info)
{
    uint64_t evt[4];

    info |= AMDVI_EVENT_IOPF_I | AMDVI_EVENT_IOPF;
    amdvi_encode_event(evt, devid, addr, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci.dev.config + PCI_STATUS,
            PCI_STATUS_SIG_TARGET_ABORT);
}
/*
 * log a master abort accessing device table
 *  @devtab : address of device table entry
 *  @info : error flags
 */
static void amdvi_log_devtab_error(AMDVIState *s, uint16_t devid,
                                   hwaddr devtab, uint16_t info)
{
    uint64_t evt[4];

    info |= AMDVI_EVENT_DEV_TAB_HW_ERROR;

    amdvi_encode_event(evt, devid, devtab, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci.dev.config + PCI_STATUS,
            PCI_STATUS_SIG_TARGET_ABORT);
}
/* log an event trying to access command buffer
 *   @addr : address that couldn't be accessed
 */
static void amdvi_log_command_error(AMDVIState *s, hwaddr addr)
{
    uint64_t evt[4], info = AMDVI_EVENT_COMMAND_HW_ERROR;

    amdvi_encode_event(evt, 0, addr, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci.dev.config + PCI_STATUS,
            PCI_STATUS_SIG_TARGET_ABORT);
}
/* log an illegal comand event
 *   @addr : address of illegal command
 */
static void amdvi_log_illegalcom_error(AMDVIState *s, uint16_t info,
                                       hwaddr addr)
{
    uint64_t evt[4];

    info |= AMDVI_EVENT_ILLEGAL_COMMAND_ERROR;
    amdvi_encode_event(evt, 0, addr, info);
    amdvi_log_event(s, evt);
}
/* log an error accessing device table
 *
 *  @devid : device owning the table entry
 *  @devtab : address of device table entry
 *  @info : error flags
 */
static void amdvi_log_illegaldevtab_error(AMDVIState *s, uint16_t devid,
                                          hwaddr addr, uint16_t info)
{
    uint64_t evt[4];

    info |= AMDVI_EVENT_ILLEGAL_DEVTAB_ENTRY;
    amdvi_encode_event(evt, devid, addr, info);
    amdvi_log_event(s, evt);
}
/* log an error accessing a PTE entry
 * @addr : address that couldn't be accessed
 */
static void amdvi_log_pagetab_error(AMDVIState *s, uint16_t devid,
                                    hwaddr addr, uint16_t info)
{
    uint64_t evt[4];

    info |= AMDVI_EVENT_PAGE_TAB_HW_ERROR;
    amdvi_encode_event(evt, devid, addr, info);
    amdvi_log_event(s, evt);
    pci_word_test_and_set_mask(s->pci.dev.config + PCI_STATUS,
             PCI_STATUS_SIG_TARGET_ABORT);
}

static gboolean amdvi_uint64_equal(gconstpointer v1, gconstpointer v2)
{
    return *((const uint64_t *)v1) == *((const uint64_t *)v2);
}

static guint amdvi_uint64_hash(gconstpointer v)
{
    return (guint)*(const uint64_t *)v;
}

static AMDVIIOTLBEntry *amdvi_iotlb_lookup(AMDVIState *s, hwaddr addr,
                                           uint64_t devid)
{
    uint64_t key = (addr >> AMDVI_PAGE_SHIFT_4K) |
                   ((uint64_t)(devid) << AMDVI_DEVID_SHIFT);
    return g_hash_table_lookup(s->iotlb, &key);
}

static void amdvi_iotlb_reset(AMDVIState *s)
{
    assert(s->iotlb);
    trace_amdvi_iotlb_reset();
    g_hash_table_remove_all(s->iotlb);
}

static gboolean amdvi_iotlb_remove_by_devid(gpointer key, gpointer value,
                                            gpointer user_data)
{
    AMDVIIOTLBEntry *entry = (AMDVIIOTLBEntry *)value;
    uint16_t devid = *(uint16_t *)user_data;
    return entry->devid == devid;
}

static void amdvi_iotlb_remove_page(AMDVIState *s, hwaddr addr,
                                    uint64_t devid)
{
    uint64_t key = (addr >> AMDVI_PAGE_SHIFT_4K) |
                   ((uint64_t)(devid) << AMDVI_DEVID_SHIFT);
    g_hash_table_remove(s->iotlb, &key);
}

static void amdvi_update_iotlb(AMDVIState *s, uint16_t devid,
                               uint64_t gpa, IOMMUTLBEntry to_cache,
                               uint16_t domid)
{
    AMDVIIOTLBEntry *entry = g_new(AMDVIIOTLBEntry, 1);
    uint64_t *key = g_new(uint64_t, 1);
    uint64_t gfn = gpa >> AMDVI_PAGE_SHIFT_4K;

    /* don't cache erroneous translations */
    if (to_cache.perm != IOMMU_NONE) {
        trace_amdvi_cache_update(domid, PCI_BUS_NUM(devid), PCI_SLOT(devid),
                PCI_FUNC(devid), gpa, to_cache.translated_addr);

        if (g_hash_table_size(s->iotlb) >= AMDVI_IOTLB_MAX_SIZE) {
            amdvi_iotlb_reset(s);
        }

        entry->domid = domid;
        entry->perms = to_cache.perm;
        entry->translated_addr = to_cache.translated_addr;
        entry->page_mask = to_cache.addr_mask;
        *key = gfn | ((uint64_t)(devid) << AMDVI_DEVID_SHIFT);
        g_hash_table_replace(s->iotlb, key, entry);
    }
}

static void amdvi_completion_wait(AMDVIState *s, uint64_t *cmd)
{
    /* pad the last 3 bits */
    hwaddr addr = cpu_to_le64(extract64(cmd[0], 3, 49)) << 3;
    uint64_t data = cpu_to_le64(cmd[1]);

    if (extract64(cmd[0], 52, 8)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
    if (extract64(cmd[0], 0, 1)) {
        if (dma_memory_write(&address_space_memory, addr, &data,
            AMDVI_COMPLETION_DATA_SIZE)) {
            trace_amdvi_completion_wait_fail(addr);
        }
    }
    /* set completion interrupt */
    if (extract64(cmd[0], 1, 1)) {
        amdvi_test_mask(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_COMP_INT);
        /* generate interrupt */
        amdvi_generate_msi_interrupt(s);
    }
    trace_amdvi_completion_wait(addr, data);
}

/* log error without aborting since linux seems to be using reserved bits */
static void amdvi_inval_devtab_entry(AMDVIState *s, uint64_t *cmd)
{
    uint16_t devid = cpu_to_le16((uint16_t)extract64(cmd[0], 0, 16));

    /* This command should invalidate internal caches of which there isn't */
    if (extract64(cmd[0], 16, 44) || cmd[1]) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
    trace_amdvi_devtab_inval(PCI_BUS_NUM(devid), PCI_SLOT(devid),
                             PCI_FUNC(devid));
}

static void amdvi_complete_ppr(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 16, 16) ||  extract64(cmd[0], 52, 8) ||
        extract64(cmd[1], 0, 2) || extract64(cmd[1], 3, 29)
        || extract64(cmd[1], 48, 16)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
    trace_amdvi_ppr_exec();
}

static void amdvi_inval_all(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 0, 60) || cmd[1]) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }

    amdvi_iotlb_reset(s);
    trace_amdvi_all_inval();
}

static gboolean amdvi_iotlb_remove_by_domid(gpointer key, gpointer value,
                                            gpointer user_data)
{
    AMDVIIOTLBEntry *entry = (AMDVIIOTLBEntry *)value;
    uint16_t domid = *(uint16_t *)user_data;
    return entry->domid == domid;
}

/* we don't have devid - we can't remove pages by address */
static void amdvi_inval_pages(AMDVIState *s, uint64_t *cmd)
{
    uint16_t domid = cpu_to_le16((uint16_t)extract64(cmd[0], 32, 16));

    if (extract64(cmd[0], 20, 12) || extract64(cmd[0], 48, 12) ||
        extract64(cmd[1], 3, 9)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }

    g_hash_table_foreach_remove(s->iotlb, amdvi_iotlb_remove_by_domid,
                                &domid);
    trace_amdvi_pages_inval(domid);
}

static void amdvi_prefetch_pages(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 16, 8) || extract64(cmd[0], 52, 8) ||
        extract64(cmd[1], 1, 1) || extract64(cmd[1], 3, 1) ||
        extract64(cmd[1], 5, 7)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }

    trace_amdvi_prefetch_pages();
}

static void amdvi_inval_inttable(AMDVIState *s, uint64_t *cmd)
{
    if (extract64(cmd[0], 16, 44) || cmd[1]) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
        return;
    }

    trace_amdvi_intr_inval();
}

/* FIXME: Try to work with the specified size instead of all the pages
 * when the S bit is on
 */
static void iommu_inval_iotlb(AMDVIState *s, uint64_t *cmd)
{

    uint16_t devid = extract64(cmd[0], 0, 16);
    if (extract64(cmd[1], 1, 1) || extract64(cmd[1], 3, 1) ||
        extract64(cmd[1], 6, 6)) {
        amdvi_log_illegalcom_error(s, extract64(cmd[0], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
        return;
    }

    if (extract64(cmd[1], 0, 1)) {
        g_hash_table_foreach_remove(s->iotlb, amdvi_iotlb_remove_by_devid,
                                    &devid);
    } else {
        amdvi_iotlb_remove_page(s, cpu_to_le64(extract64(cmd[1], 12, 52)) << 12,
                                cpu_to_le16(extract64(cmd[1], 0, 16)));
    }
    trace_amdvi_iotlb_inval();
}

/* not honouring reserved bits is regarded as an illegal command */
static void amdvi_cmdbuf_exec(AMDVIState *s)
{
    uint64_t cmd[2];

    if (dma_memory_read(&address_space_memory, s->cmdbuf + s->cmdbuf_head,
        cmd, AMDVI_COMMAND_SIZE)) {
        trace_amdvi_command_read_fail(s->cmdbuf, s->cmdbuf_head);
        amdvi_log_command_error(s, s->cmdbuf + s->cmdbuf_head);
        return;
    }

    switch (extract64(cmd[0], 60, 4)) {
    case AMDVI_CMD_COMPLETION_WAIT:
        amdvi_completion_wait(s, cmd);
        break;
    case AMDVI_CMD_INVAL_DEVTAB_ENTRY:
        amdvi_inval_devtab_entry(s, cmd);
        break;
    case AMDVI_CMD_INVAL_AMDVI_PAGES:
        amdvi_inval_pages(s, cmd);
        break;
    case AMDVI_CMD_INVAL_IOTLB_PAGES:
        iommu_inval_iotlb(s, cmd);
        break;
    case AMDVI_CMD_INVAL_INTR_TABLE:
        amdvi_inval_inttable(s, cmd);
        break;
    case AMDVI_CMD_PREFETCH_AMDVI_PAGES:
        amdvi_prefetch_pages(s, cmd);
        break;
    case AMDVI_CMD_COMPLETE_PPR_REQUEST:
        amdvi_complete_ppr(s, cmd);
        break;
    case AMDVI_CMD_INVAL_AMDVI_ALL:
        amdvi_inval_all(s, cmd);
        break;
    default:
        trace_amdvi_unhandled_command(extract64(cmd[1], 60, 4));
        /* log illegal command */
        amdvi_log_illegalcom_error(s, extract64(cmd[1], 60, 4),
                                   s->cmdbuf + s->cmdbuf_head);
    }
}

static void amdvi_cmdbuf_run(AMDVIState *s)
{
    if (!s->cmdbuf_enabled) {
        trace_amdvi_command_error(amdvi_readq(s, AMDVI_MMIO_CONTROL));
        return;
    }

    /* check if there is work to do. */
    while (s->cmdbuf_head != s->cmdbuf_tail) {
        trace_amdvi_command_exec(s->cmdbuf_head, s->cmdbuf_tail, s->cmdbuf);
        amdvi_cmdbuf_exec(s);
        s->cmdbuf_head += AMDVI_COMMAND_SIZE;
        amdvi_writeq_raw(s, s->cmdbuf_head, AMDVI_MMIO_COMMAND_HEAD);

        /* wrap head pointer */
        if (s->cmdbuf_head >= s->cmdbuf_len * AMDVI_COMMAND_SIZE) {
            s->cmdbuf_head = 0;
        }
    }
}

static void amdvi_mmio_trace(hwaddr addr, unsigned size)
{
    uint8_t index = (addr & ~0x2000) / 8;

    if ((addr & 0x2000)) {
        /* high table */
        index = index >= AMDVI_MMIO_REGS_HIGH ? AMDVI_MMIO_REGS_HIGH : index;
        trace_amdvi_mmio_read(amdvi_mmio_high[index], addr, size, addr & ~0x07);
    } else {
        index = index >= AMDVI_MMIO_REGS_LOW ? AMDVI_MMIO_REGS_LOW : index;
        trace_amdvi_mmio_read(amdvi_mmio_low[index], addr, size, addr & ~0x07);
    }
}

static uint64_t amdvi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    AMDVIState *s = opaque;

    uint64_t val = -1;
    if (addr + size > AMDVI_MMIO_SIZE) {
        trace_amdvi_mmio_read_invalid(AMDVI_MMIO_SIZE, addr, size);
        return (uint64_t)-1;
    }

    if (size == 2) {
        val = amdvi_readw(s, addr);
    } else if (size == 4) {
        val = amdvi_readl(s, addr);
    } else if (size == 8) {
        val = amdvi_readq(s, addr);
    }
    amdvi_mmio_trace(addr, size);

    return val;
}

static void amdvi_handle_control_write(AMDVIState *s)
{
    unsigned long control = amdvi_readq(s, AMDVI_MMIO_CONTROL);
    s->enabled = !!(control & AMDVI_MMIO_CONTROL_AMDVIEN);

    s->ats_enabled = !!(control & AMDVI_MMIO_CONTROL_HTTUNEN);
    s->evtlog_enabled = s->enabled && !!(control &
                        AMDVI_MMIO_CONTROL_EVENTLOGEN);

    s->evtlog_intr = !!(control & AMDVI_MMIO_CONTROL_EVENTINTEN);
    s->completion_wait_intr = !!(control & AMDVI_MMIO_CONTROL_COMWAITINTEN);
    s->cmdbuf_enabled = s->enabled && !!(control &
                        AMDVI_MMIO_CONTROL_CMDBUFLEN);
    s->ga_enabled = !!(control & AMDVI_MMIO_CONTROL_GAEN);

    /* update the flags depending on the control register */
    if (s->cmdbuf_enabled) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_CMDBUF_RUN);
    } else {
        amdvi_assign_andq(s, AMDVI_MMIO_STATUS, ~AMDVI_MMIO_STATUS_CMDBUF_RUN);
    }
    if (s->evtlog_enabled) {
        amdvi_assign_orq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_EVT_RUN);
    } else {
        amdvi_assign_andq(s, AMDVI_MMIO_STATUS, ~AMDVI_MMIO_STATUS_EVT_RUN);
    }

    trace_amdvi_control_status(control);
    amdvi_cmdbuf_run(s);
}

static inline void amdvi_handle_devtab_write(AMDVIState *s)

{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_DEVICE_TABLE);
    s->devtab = (val & AMDVI_MMIO_DEVTAB_BASE_MASK);

    /* set device table length */
    s->devtab_len = ((val & AMDVI_MMIO_DEVTAB_SIZE_MASK) + 1 *
                    (AMDVI_MMIO_DEVTAB_SIZE_UNIT /
                     AMDVI_MMIO_DEVTAB_ENTRY_SIZE));
}

static inline void amdvi_handle_cmdhead_write(AMDVIState *s)
{
    s->cmdbuf_head = amdvi_readq(s, AMDVI_MMIO_COMMAND_HEAD)
                     & AMDVI_MMIO_CMDBUF_HEAD_MASK;
    amdvi_cmdbuf_run(s);
}

static inline void amdvi_handle_cmdbase_write(AMDVIState *s)
{
    s->cmdbuf = amdvi_readq(s, AMDVI_MMIO_COMMAND_BASE)
                & AMDVI_MMIO_CMDBUF_BASE_MASK;
    s->cmdbuf_len = 1UL << (amdvi_readq(s, AMDVI_MMIO_CMDBUF_SIZE_BYTE)
                    & AMDVI_MMIO_CMDBUF_SIZE_MASK);
    s->cmdbuf_head = s->cmdbuf_tail = 0;
}

static inline void amdvi_handle_cmdtail_write(AMDVIState *s)
{
    s->cmdbuf_tail = amdvi_readq(s, AMDVI_MMIO_COMMAND_TAIL)
                     & AMDVI_MMIO_CMDBUF_TAIL_MASK;
    amdvi_cmdbuf_run(s);
}

static inline void amdvi_handle_excllim_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EXCL_LIMIT);
    s->excl_limit = (val & AMDVI_MMIO_EXCL_LIMIT_MASK) |
                    AMDVI_MMIO_EXCL_LIMIT_LOW;
}

static inline void amdvi_handle_evtbase_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EVENT_BASE);
    s->evtlog = val & AMDVI_MMIO_EVTLOG_BASE_MASK;
    s->evtlog_len = 1UL << (amdvi_readq(s, AMDVI_MMIO_EVTLOG_SIZE_BYTE)
                    & AMDVI_MMIO_EVTLOG_SIZE_MASK);
}

static inline void amdvi_handle_evttail_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EVENT_TAIL);
    s->evtlog_tail = val & AMDVI_MMIO_EVTLOG_TAIL_MASK;
}

static inline void amdvi_handle_evthead_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_EVENT_HEAD);
    s->evtlog_head = val & AMDVI_MMIO_EVTLOG_HEAD_MASK;
}

static inline void amdvi_handle_pprbase_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_PPR_BASE);
    s->ppr_log = val & AMDVI_MMIO_PPRLOG_BASE_MASK;
    s->pprlog_len = 1UL << (amdvi_readq(s, AMDVI_MMIO_PPRLOG_SIZE_BYTE)
                    & AMDVI_MMIO_PPRLOG_SIZE_MASK);
}

static inline void amdvi_handle_pprhead_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_PPR_HEAD);
    s->pprlog_head = val & AMDVI_MMIO_PPRLOG_HEAD_MASK;
}

static inline void amdvi_handle_pprtail_write(AMDVIState *s)
{
    uint64_t val = amdvi_readq(s, AMDVI_MMIO_PPR_TAIL);
    s->pprlog_tail = val & AMDVI_MMIO_PPRLOG_TAIL_MASK;
}

/* FIXME: something might go wrong if System Software writes in chunks
 * of one byte but linux writes in chunks of 4 bytes so currently it
 * works correctly with linux but will definitely be busted if software
 * reads/writes 8 bytes
 */
static void amdvi_mmio_reg_write(AMDVIState *s, unsigned size, uint64_t val,
                                 hwaddr addr)
{
    if (size == 2) {
        amdvi_writew(s, addr, val);
    } else if (size == 4) {
        amdvi_writel(s, addr, val);
    } else if (size == 8) {
        amdvi_writeq(s, addr, val);
    }
}

static void amdvi_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    AMDVIState *s = opaque;
    unsigned long offset = addr & 0x07;

    if (addr + size > AMDVI_MMIO_SIZE) {
        trace_amdvi_mmio_write("error: addr outside region: max ",
                (uint64_t)AMDVI_MMIO_SIZE, size, val, offset);
        return;
    }

    amdvi_mmio_trace(addr, size);
    switch (addr & ~0x07) {
    case AMDVI_MMIO_CONTROL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_control_write(s);
        break;
    case AMDVI_MMIO_DEVICE_TABLE:
        amdvi_mmio_reg_write(s, size, val, addr);
       /*  set device table address
        *   This also suffers from inability to tell whether software
        *   is done writing
        */
        if (offset || (size == 8)) {
            amdvi_handle_devtab_write(s);
        }
        break;
    case AMDVI_MMIO_COMMAND_HEAD:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_cmdhead_write(s);
        break;
    case AMDVI_MMIO_COMMAND_BASE:
        amdvi_mmio_reg_write(s, size, val, addr);
        /* FIXME - make sure System Software has finished writing incase
         * it writes in chucks less than 8 bytes in a robust way.As for
         * now, this hacks works for the linux driver
         */
        if (offset || (size == 8)) {
            amdvi_handle_cmdbase_write(s);
        }
        break;
    case AMDVI_MMIO_COMMAND_TAIL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_cmdtail_write(s);
        break;
    case AMDVI_MMIO_EVENT_BASE:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_evtbase_write(s);
        break;
    case AMDVI_MMIO_EVENT_HEAD:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_evthead_write(s);
        break;
    case AMDVI_MMIO_EVENT_TAIL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_evttail_write(s);
        break;
    case AMDVI_MMIO_EXCL_LIMIT:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_excllim_write(s);
        break;
        /* PPR log base - unused for now */
    case AMDVI_MMIO_PPR_BASE:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_pprbase_write(s);
        break;
        /* PPR log head - also unused for now */
    case AMDVI_MMIO_PPR_HEAD:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_pprhead_write(s);
        break;
        /* PPR log tail - unused for now */
    case AMDVI_MMIO_PPR_TAIL:
        amdvi_mmio_reg_write(s, size, val, addr);
        amdvi_handle_pprtail_write(s);
        break;
    }
}

static inline uint64_t amdvi_get_perms(uint64_t entry)
{
    return (entry & (AMDVI_DEV_PERM_READ | AMDVI_DEV_PERM_WRITE)) >>
           AMDVI_DEV_PERM_SHIFT;
}

/* validate that reserved bits are honoured */
static bool amdvi_validate_dte(AMDVIState *s, uint16_t devid,
                               uint64_t *dte)
{
    if ((dte[0] & AMDVI_DTE_LOWER_QUAD_RESERVED)
        || (dte[1] & AMDVI_DTE_MIDDLE_QUAD_RESERVED)
        || (dte[2] & AMDVI_DTE_UPPER_QUAD_RESERVED) || dte[3]) {
        amdvi_log_illegaldevtab_error(s, devid,
                                      s->devtab +
                                      devid * AMDVI_DEVTAB_ENTRY_SIZE, 0);
        return false;
    }

    return true;
}

/* get a device table entry given the devid */
static bool amdvi_get_dte(AMDVIState *s, int devid, uint64_t *entry)
{
    uint32_t offset = devid * AMDVI_DEVTAB_ENTRY_SIZE;

    if (dma_memory_read(&address_space_memory, s->devtab + offset, entry,
        AMDVI_DEVTAB_ENTRY_SIZE)) {
        trace_amdvi_dte_get_fail(s->devtab, offset);
        /* log error accessing dte */
        amdvi_log_devtab_error(s, devid, s->devtab + offset, 0);
        return false;
    }

    *entry = le64_to_cpu(*entry);
    if (!amdvi_validate_dte(s, devid, entry)) {
        trace_amdvi_invalid_dte(entry[0]);
        return false;
    }

    return true;
}

/* get pte translation mode */
static inline uint8_t get_pte_translation_mode(uint64_t pte)
{
    return (pte >> AMDVI_DEV_MODE_RSHIFT) & AMDVI_DEV_MODE_MASK;
}

static inline uint64_t pte_override_page_mask(uint64_t pte)
{
    uint8_t page_mask = 12;
    uint64_t addr = (pte & AMDVI_DEV_PT_ROOT_MASK) ^ AMDVI_DEV_PT_ROOT_MASK;
    /* find the first zero bit */
    while (addr & 1) {
        page_mask++;
        addr = addr >> 1;
    }

    return ~((1ULL << page_mask) - 1);
}

static inline uint64_t pte_get_page_mask(uint64_t oldlevel)
{
    return ~((1UL << ((oldlevel * 9) + 3)) - 1);
}

static inline uint64_t amdvi_get_pte_entry(AMDVIState *s, uint64_t pte_addr,
                                          uint16_t devid)
{
    uint64_t pte;

    if (dma_memory_read(&address_space_memory, pte_addr, &pte, sizeof(pte))) {
        trace_amdvi_get_pte_hwerror(pte_addr);
        amdvi_log_pagetab_error(s, devid, pte_addr, 0);
        pte = 0;
        return pte;
    }

    pte = le64_to_cpu(pte);
    return pte;
}

static void amdvi_page_walk(AMDVIAddressSpace *as, uint64_t *dte,
                            IOMMUTLBEntry *ret, unsigned perms,
                            hwaddr addr)
{
    unsigned level, present, pte_perms, oldlevel;
    uint64_t pte = dte[0], pte_addr, page_mask;

    /* make sure the DTE has TV = 1 */
    if (pte & AMDVI_DEV_TRANSLATION_VALID) {
        level = get_pte_translation_mode(pte);
        if (level >= 7) {
            trace_amdvi_mode_invalid(level, addr);
            return;
        }
        if (level == 0) {
            goto no_remap;
        }

        /* we are at the leaf page table or page table encodes a huge page */
        while (level > 0) {
            pte_perms = amdvi_get_perms(pte);
            present = pte & 1;
            if (!present || perms != (perms & pte_perms)) {
                amdvi_page_fault(as->iommu_state, as->devfn, addr, perms);
                trace_amdvi_page_fault(addr);
                return;
            }

            /* go to the next lower level */
            pte_addr = pte & AMDVI_DEV_PT_ROOT_MASK;
            /* add offset and load pte */
            pte_addr += ((addr >> (3 + 9 * level)) & 0x1FF) << 3;
            pte = amdvi_get_pte_entry(as->iommu_state, pte_addr, as->devfn);
            if (!pte) {
                return;
            }
            oldlevel = level;
            level = get_pte_translation_mode(pte);
            if (level == 0x7) {
                break;
            }
        }

        if (level == 0x7) {
            page_mask = pte_override_page_mask(pte);
        } else {
            page_mask = pte_get_page_mask(oldlevel);
        }

        /* get access permissions from pte */
        ret->iova = addr & page_mask;
        ret->translated_addr = (pte & AMDVI_DEV_PT_ROOT_MASK) & page_mask;
        ret->addr_mask = ~page_mask;
        ret->perm = amdvi_get_perms(pte);
        return;
    }
no_remap:
    ret->iova = addr & AMDVI_PAGE_MASK_4K;
    ret->translated_addr = addr & AMDVI_PAGE_MASK_4K;
    ret->addr_mask = ~AMDVI_PAGE_MASK_4K;
    ret->perm = amdvi_get_perms(pte);
}

static void amdvi_do_translate(AMDVIAddressSpace *as, hwaddr addr,
                               bool is_write, IOMMUTLBEntry *ret)
{
    AMDVIState *s = as->iommu_state;
    uint16_t devid = PCI_BUILD_BDF(as->bus_num, as->devfn);
    AMDVIIOTLBEntry *iotlb_entry = amdvi_iotlb_lookup(s, addr, devid);
    uint64_t entry[4];

    if (iotlb_entry) {
        trace_amdvi_iotlb_hit(PCI_BUS_NUM(devid), PCI_SLOT(devid),
                PCI_FUNC(devid), addr, iotlb_entry->translated_addr);
        ret->iova = addr & ~iotlb_entry->page_mask;
        ret->translated_addr = iotlb_entry->translated_addr;
        ret->addr_mask = iotlb_entry->page_mask;
        ret->perm = iotlb_entry->perms;
        return;
    }

    if (!amdvi_get_dte(s, devid, entry)) {
        return;
    }

    /* devices with V = 0 are not translated */
    if (!(entry[0] & AMDVI_DEV_VALID)) {
        goto out;
    }

    amdvi_page_walk(as, entry, ret,
                    is_write ? AMDVI_PERM_WRITE : AMDVI_PERM_READ, addr);

    amdvi_update_iotlb(s, devid, addr, *ret,
                       entry[1] & AMDVI_DEV_DOMID_ID_MASK);
    return;

out:
    ret->iova = addr & AMDVI_PAGE_MASK_4K;
    ret->translated_addr = addr & AMDVI_PAGE_MASK_4K;
    ret->addr_mask = ~AMDVI_PAGE_MASK_4K;
    ret->perm = IOMMU_RW;
}

static inline bool amdvi_is_interrupt_addr(hwaddr addr)
{
    return addr >= AMDVI_INT_ADDR_FIRST && addr <= AMDVI_INT_ADDR_LAST;
}

static IOMMUTLBEntry amdvi_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                     IOMMUAccessFlags flag, int iommu_idx)
{
    AMDVIAddressSpace *as = container_of(iommu, AMDVIAddressSpace, iommu);
    AMDVIState *s = as->iommu_state;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE
    };

    if (!s->enabled) {
        /* AMDVI disabled - corresponds to iommu=off not
         * failure to provide any parameter
         */
        ret.iova = addr & AMDVI_PAGE_MASK_4K;
        ret.translated_addr = addr & AMDVI_PAGE_MASK_4K;
        ret.addr_mask = ~AMDVI_PAGE_MASK_4K;
        ret.perm = IOMMU_RW;
        return ret;
    } else if (amdvi_is_interrupt_addr(addr)) {
        ret.iova = addr & AMDVI_PAGE_MASK_4K;
        ret.translated_addr = addr & AMDVI_PAGE_MASK_4K;
        ret.addr_mask = ~AMDVI_PAGE_MASK_4K;
        ret.perm = IOMMU_WO;
        return ret;
    }

    amdvi_do_translate(as, addr, flag & IOMMU_WO, &ret);
    trace_amdvi_translation_result(as->bus_num, PCI_SLOT(as->devfn),
            PCI_FUNC(as->devfn), addr, ret.translated_addr);
    return ret;
}

static int amdvi_get_irte(AMDVIState *s, MSIMessage *origin, uint64_t *dte,
                          union irte *irte, uint16_t devid)
{
    uint64_t irte_root, offset;

    irte_root = dte[2] & AMDVI_IR_PHYS_ADDR_MASK;
    offset = (origin->data & AMDVI_IRTE_OFFSET) << 2;

    trace_amdvi_ir_irte(irte_root, offset);

    if (dma_memory_read(&address_space_memory, irte_root + offset,
                        irte, sizeof(*irte))) {
        trace_amdvi_ir_err("failed to get irte");
        return -AMDVI_IR_GET_IRTE;
    }

    trace_amdvi_ir_irte_val(irte->val);

    return 0;
}

static int amdvi_int_remap_legacy(AMDVIState *iommu,
                                  MSIMessage *origin,
                                  MSIMessage *translated,
                                  uint64_t *dte,
                                  X86IOMMUIrq *irq,
                                  uint16_t sid)
{
    int ret;
    union irte irte;

    /* get interrupt remapping table */
    ret = amdvi_get_irte(iommu, origin, dte, &irte, sid);
    if (ret < 0) {
        return ret;
    }

    if (!irte.fields.valid) {
        trace_amdvi_ir_target_abort("RemapEn is disabled");
        return -AMDVI_IR_TARGET_ABORT;
    }

    if (irte.fields.guest_mode) {
        error_report_once("guest mode is not zero");
        return -AMDVI_IR_ERR;
    }

    if (irte.fields.int_type > AMDVI_IOAPIC_INT_TYPE_ARBITRATED) {
        error_report_once("reserved int_type");
        return -AMDVI_IR_ERR;
    }

    irq->delivery_mode = irte.fields.int_type;
    irq->vector = irte.fields.vector;
    irq->dest_mode = irte.fields.dm;
    irq->redir_hint = irte.fields.rq_eoi;
    irq->dest = irte.fields.destination;

    return 0;
}

static int amdvi_get_irte_ga(AMDVIState *s, MSIMessage *origin, uint64_t *dte,
                             struct irte_ga *irte, uint16_t devid)
{
    uint64_t irte_root, offset;

    irte_root = dte[2] & AMDVI_IR_PHYS_ADDR_MASK;
    offset = (origin->data & AMDVI_IRTE_OFFSET) << 4;
    trace_amdvi_ir_irte(irte_root, offset);

    if (dma_memory_read(&address_space_memory, irte_root + offset,
                        irte, sizeof(*irte))) {
        trace_amdvi_ir_err("failed to get irte_ga");
        return -AMDVI_IR_GET_IRTE;
    }

    trace_amdvi_ir_irte_ga_val(irte->hi.val, irte->lo.val);
    return 0;
}

static int amdvi_int_remap_ga(AMDVIState *iommu,
                              MSIMessage *origin,
                              MSIMessage *translated,
                              uint64_t *dte,
                              X86IOMMUIrq *irq,
                              uint16_t sid)
{
    int ret;
    struct irte_ga irte;

    /* get interrupt remapping table */
    ret = amdvi_get_irte_ga(iommu, origin, dte, &irte, sid);
    if (ret < 0) {
        return ret;
    }

    if (!irte.lo.fields_remap.valid) {
        trace_amdvi_ir_target_abort("RemapEn is disabled");
        return -AMDVI_IR_TARGET_ABORT;
    }

    if (irte.lo.fields_remap.guest_mode) {
        error_report_once("guest mode is not zero");
        return -AMDVI_IR_ERR;
    }

    if (irte.lo.fields_remap.int_type > AMDVI_IOAPIC_INT_TYPE_ARBITRATED) {
        error_report_once("reserved int_type is set");
        return -AMDVI_IR_ERR;
    }

    irq->delivery_mode = irte.lo.fields_remap.int_type;
    irq->vector = irte.hi.fields.vector;
    irq->dest_mode = irte.lo.fields_remap.dm;
    irq->redir_hint = irte.lo.fields_remap.rq_eoi;
    irq->dest = irte.lo.fields_remap.destination;

    return 0;
}

static int __amdvi_int_remap_msi(AMDVIState *iommu,
                                 MSIMessage *origin,
                                 MSIMessage *translated,
                                 uint64_t *dte,
                                 X86IOMMUIrq *irq,
                                 uint16_t sid)
{
    int ret;
    uint8_t int_ctl;

    int_ctl = (dte[2] >> AMDVI_IR_INTCTL_SHIFT) & 3;
    trace_amdvi_ir_intctl(int_ctl);

    switch (int_ctl) {
    case AMDVI_IR_INTCTL_PASS:
        memcpy(translated, origin, sizeof(*origin));
        return 0;
    case AMDVI_IR_INTCTL_REMAP:
        break;
    case AMDVI_IR_INTCTL_ABORT:
        trace_amdvi_ir_target_abort("int_ctl abort");
        return -AMDVI_IR_TARGET_ABORT;
    default:
        trace_amdvi_ir_err("int_ctl reserved");
        return -AMDVI_IR_ERR;
    }

    if (iommu->ga_enabled) {
        ret = amdvi_int_remap_ga(iommu, origin, translated, dte, irq, sid);
    } else {
        ret = amdvi_int_remap_legacy(iommu, origin, translated, dte, irq, sid);
    }

    return ret;
}

/* Interrupt remapping for MSI/MSI-X entry */
static int amdvi_int_remap_msi(AMDVIState *iommu,
                               MSIMessage *origin,
                               MSIMessage *translated,
                               uint16_t sid)
{
    int ret = 0;
    uint64_t pass = 0;
    uint64_t dte[4] = { 0 };
    X86IOMMUIrq irq = { 0 };
    uint8_t dest_mode, delivery_mode;

    assert(origin && translated);

    /*
     * When IOMMU is enabled, interrupt remap request will come either from
     * IO-APIC or PCI device. If interrupt is from PCI device then it will
     * have a valid requester id but if the interrupt is from IO-APIC
     * then requester id will be invalid.
     */
    if (sid == X86_IOMMU_SID_INVALID) {
        sid = AMDVI_IOAPIC_SB_DEVID;
    }

    trace_amdvi_ir_remap_msi_req(origin->address, origin->data, sid);

    /* check if device table entry is set before we go further. */
    if (!iommu || !iommu->devtab_len) {
        memcpy(translated, origin, sizeof(*origin));
        goto out;
    }

    if (!amdvi_get_dte(iommu, sid, dte)) {
        return -AMDVI_IR_ERR;
    }

    /* Check if IR is enabled in DTE */
    if (!(dte[2] & AMDVI_IR_REMAP_ENABLE)) {
        memcpy(translated, origin, sizeof(*origin));
        goto out;
    }

    /* validate that we are configure with intremap=on */
    if (!x86_iommu_ir_supported(X86_IOMMU_DEVICE(iommu))) {
        trace_amdvi_err("Interrupt remapping is enabled in the guest but "
                        "not in the host. Use intremap=on to enable interrupt "
                        "remapping in amd-iommu.");
        return -AMDVI_IR_ERR;
    }

    if (origin->address & AMDVI_MSI_ADDR_HI_MASK) {
        trace_amdvi_err("MSI address high 32 bits non-zero when "
                        "Interrupt Remapping enabled.");
        return -AMDVI_IR_ERR;
    }

    if ((origin->address & AMDVI_MSI_ADDR_LO_MASK) != APIC_DEFAULT_ADDRESS) {
        trace_amdvi_err("MSI is not from IOAPIC.");
        return -AMDVI_IR_ERR;
    }

    /*
     * The MSI data register [10:8] are used to get the upstream interrupt type.
     *
     * See MSI/MSI-X format:
     * https://pdfs.semanticscholar.org/presentation/9420/c279e942eca568157711ef5c92b800c40a79.pdf
     * (page 5)
     */
    delivery_mode = (origin->data >> MSI_DATA_DELIVERY_MODE_SHIFT) & 7;

    switch (delivery_mode) {
    case AMDVI_IOAPIC_INT_TYPE_FIXED:
    case AMDVI_IOAPIC_INT_TYPE_ARBITRATED:
        trace_amdvi_ir_delivery_mode("fixed/arbitrated");
        ret = __amdvi_int_remap_msi(iommu, origin, translated, dte, &irq, sid);
        if (ret < 0) {
            goto remap_fail;
        } else {
            /* Translate IRQ to MSI messages */
            x86_iommu_irq_to_msi_message(&irq, translated);
            goto out;
        }
        break;
    case AMDVI_IOAPIC_INT_TYPE_SMI:
        error_report("SMI is not supported!");
        ret = -AMDVI_IR_ERR;
        break;
    case AMDVI_IOAPIC_INT_TYPE_NMI:
        pass = dte[3] & AMDVI_DEV_NMI_PASS_MASK;
        trace_amdvi_ir_delivery_mode("nmi");
        break;
    case AMDVI_IOAPIC_INT_TYPE_INIT:
        pass = dte[3] & AMDVI_DEV_INT_PASS_MASK;
        trace_amdvi_ir_delivery_mode("init");
        break;
    case AMDVI_IOAPIC_INT_TYPE_EINT:
        pass = dte[3] & AMDVI_DEV_EINT_PASS_MASK;
        trace_amdvi_ir_delivery_mode("eint");
        break;
    default:
        trace_amdvi_ir_delivery_mode("unsupported delivery_mode");
        ret = -AMDVI_IR_ERR;
        break;
    }

    if (ret < 0) {
        goto remap_fail;
    }

    /*
     * The MSI address register bit[2] is used to get the destination
     * mode. The dest_mode 1 is valid for fixed and arbitrated interrupts
     * only.
     */
    dest_mode = (origin->address >> MSI_ADDR_DEST_MODE_SHIFT) & 1;
    if (dest_mode) {
        trace_amdvi_ir_err("invalid dest_mode");
        ret = -AMDVI_IR_ERR;
        goto remap_fail;
    }

    if (pass) {
        memcpy(translated, origin, sizeof(*origin));
    } else {
        trace_amdvi_ir_err("passthrough is not enabled");
        ret = -AMDVI_IR_ERR;
        goto remap_fail;
    }

out:
    trace_amdvi_ir_remap_msi(origin->address, origin->data,
                             translated->address, translated->data);
    return 0;

remap_fail:
    return ret;
}

static int amdvi_int_remap(X86IOMMUState *iommu,
                           MSIMessage *origin,
                           MSIMessage *translated,
                           uint16_t sid)
{
    return amdvi_int_remap_msi(AMD_IOMMU_DEVICE(iommu), origin,
                               translated, sid);
}

static MemTxResult amdvi_mem_ir_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    int ret;
    MSIMessage from = { 0, 0 }, to = { 0, 0 };
    uint16_t sid = AMDVI_IOAPIC_SB_DEVID;

    from.address = (uint64_t) addr + AMDVI_INT_ADDR_FIRST;
    from.data = (uint32_t) value;

    trace_amdvi_mem_ir_write_req(addr, value, size);

    if (!attrs.unspecified) {
        /* We have explicit Source ID */
        sid = attrs.requester_id;
    }

    ret = amdvi_int_remap_msi(opaque, &from, &to, sid);
    if (ret < 0) {
        /* TODO: log the event using IOMMU log event interface */
        error_report_once("failed to remap interrupt from devid 0x%x", sid);
        return MEMTX_ERROR;
    }

    apic_get_class()->send_msi(&to);

    trace_amdvi_mem_ir_write(to.address, to.data);
    return MEMTX_OK;
}

static MemTxResult amdvi_mem_ir_read(void *opaque, hwaddr addr,
                                     uint64_t *data, unsigned size,
                                     MemTxAttrs attrs)
{
    return MEMTX_OK;
}

static const MemoryRegionOps amdvi_ir_ops = {
    .read_with_attrs = amdvi_mem_ir_read,
    .write_with_attrs = amdvi_mem_ir_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static AddressSpace *amdvi_host_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    char name[128];
    AMDVIState *s = opaque;
    AMDVIAddressSpace **iommu_as, *amdvi_dev_as;
    int bus_num = pci_bus_num(bus);

    iommu_as = s->address_spaces[bus_num];

    /* allocate memory during the first run */
    if (!iommu_as) {
        iommu_as = g_malloc0(sizeof(AMDVIAddressSpace *) * PCI_DEVFN_MAX);
        s->address_spaces[bus_num] = iommu_as;
    }

    /* set up AMD-Vi region */
    if (!iommu_as[devfn]) {
        snprintf(name, sizeof(name), "amd_iommu_devfn_%d", devfn);

        iommu_as[devfn] = g_malloc0(sizeof(AMDVIAddressSpace));
        iommu_as[devfn]->bus_num = (uint8_t)bus_num;
        iommu_as[devfn]->devfn = (uint8_t)devfn;
        iommu_as[devfn]->iommu_state = s;

        amdvi_dev_as = iommu_as[devfn];

        /*
         * Memory region relationships looks like (Address range shows
         * only lower 32 bits to make it short in length...):
         *
         * |-----------------+-------------------+----------|
         * | Name            | Address range     | Priority |
         * |-----------------+-------------------+----------+
         * | amdvi_root      | 00000000-ffffffff |        0 |
         * |  amdvi_iommu    | 00000000-ffffffff |        1 |
         * |  amdvi_iommu_ir | fee00000-feefffff |       64 |
         * |-----------------+-------------------+----------|
         */
        memory_region_init_iommu(&amdvi_dev_as->iommu,
                                 sizeof(amdvi_dev_as->iommu),
                                 TYPE_AMD_IOMMU_MEMORY_REGION,
                                 OBJECT(s),
                                 "amd_iommu", UINT64_MAX);
        memory_region_init(&amdvi_dev_as->root, OBJECT(s),
                           "amdvi_root", UINT64_MAX);
        address_space_init(&amdvi_dev_as->as, &amdvi_dev_as->root, name);
        memory_region_init_io(&amdvi_dev_as->iommu_ir, OBJECT(s),
                              &amdvi_ir_ops, s, "amd_iommu_ir",
                              AMDVI_INT_ADDR_SIZE);
        memory_region_add_subregion_overlap(&amdvi_dev_as->root,
                                            AMDVI_INT_ADDR_FIRST,
                                            &amdvi_dev_as->iommu_ir,
                                            64);
        memory_region_add_subregion_overlap(&amdvi_dev_as->root, 0,
                                            MEMORY_REGION(&amdvi_dev_as->iommu),
                                            1);
    }
    return &iommu_as[devfn]->as;
}

static const MemoryRegionOps mmio_mem_ops = {
    .read = amdvi_mmio_read,
    .write = amdvi_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    }
};

static int amdvi_iommu_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                           IOMMUNotifierFlag old,
                                           IOMMUNotifierFlag new,
                                           Error **errp)
{
    AMDVIAddressSpace *as = container_of(iommu, AMDVIAddressSpace, iommu);

    if (new & IOMMU_NOTIFIER_MAP) {
        error_setg(errp,
                   "device %02x.%02x.%x requires iommu notifier which is not "
                   "currently supported", as->bus_num, PCI_SLOT(as->devfn),
                   PCI_FUNC(as->devfn));
        return -EINVAL;
    }
    return 0;
}

static void amdvi_init(AMDVIState *s)
{
    amdvi_iotlb_reset(s);

    s->devtab_len = 0;
    s->cmdbuf_len = 0;
    s->cmdbuf_head = 0;
    s->cmdbuf_tail = 0;
    s->evtlog_head = 0;
    s->evtlog_tail = 0;
    s->excl_enabled = false;
    s->excl_allow = false;
    s->mmio_enabled = false;
    s->enabled = false;
    s->ats_enabled = false;
    s->cmdbuf_enabled = false;

    /* reset MMIO */
    memset(s->mmior, 0, AMDVI_MMIO_SIZE);
    amdvi_set_quad(s, AMDVI_MMIO_EXT_FEATURES, AMDVI_EXT_FEATURES,
            0xffffffffffffffef, 0);
    amdvi_set_quad(s, AMDVI_MMIO_STATUS, 0, 0x98, 0x67);

    /* reset device ident */
    pci_config_set_vendor_id(s->pci.dev.config, PCI_VENDOR_ID_AMD);
    pci_config_set_prog_interface(s->pci.dev.config, 00);
    pci_config_set_device_id(s->pci.dev.config, s->devid);
    pci_config_set_class(s->pci.dev.config, 0x0806);

    /* reset AMDVI specific capabilities, all r/o */
    pci_set_long(s->pci.dev.config + s->capab_offset, AMDVI_CAPAB_FEATURES);
    pci_set_long(s->pci.dev.config + s->capab_offset + AMDVI_CAPAB_BAR_LOW,
                 s->mmio.addr & ~(0xffff0000));
    pci_set_long(s->pci.dev.config + s->capab_offset + AMDVI_CAPAB_BAR_HIGH,
                (s->mmio.addr & ~(0xffff)) >> 16);
    pci_set_long(s->pci.dev.config + s->capab_offset + AMDVI_CAPAB_RANGE,
                 0xff000000);
    pci_set_long(s->pci.dev.config + s->capab_offset + AMDVI_CAPAB_MISC, 0);
    pci_set_long(s->pci.dev.config + s->capab_offset + AMDVI_CAPAB_MISC,
            AMDVI_MAX_PH_ADDR | AMDVI_MAX_GVA_ADDR | AMDVI_MAX_VA_ADDR);
}

static void amdvi_reset(DeviceState *dev)
{
    AMDVIState *s = AMD_IOMMU_DEVICE(dev);

    msi_reset(&s->pci.dev);
    amdvi_init(s);
}

static void amdvi_realize(DeviceState *dev, Error **errp)
{
    int ret = 0;
    AMDVIState *s = AMD_IOMMU_DEVICE(dev);
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    PCMachineState *pcms = PC_MACHINE(ms);
    X86MachineState *x86ms = X86_MACHINE(ms);
    PCIBus *bus = pcms->bus;

    s->iotlb = g_hash_table_new_full(amdvi_uint64_hash,
                                     amdvi_uint64_equal, g_free, g_free);

    /* This device should take care of IOMMU PCI properties */
    x86_iommu->type = TYPE_AMD;
    if (!qdev_realize(DEVICE(&s->pci), &bus->qbus, errp)) {
        return;
    }
    ret = pci_add_capability(&s->pci.dev, AMDVI_CAPAB_ID_SEC, 0,
                                         AMDVI_CAPAB_SIZE, errp);
    if (ret < 0) {
        return;
    }
    s->capab_offset = ret;

    ret = pci_add_capability(&s->pci.dev, PCI_CAP_ID_MSI, 0,
                             AMDVI_CAPAB_REG_SIZE, errp);
    if (ret < 0) {
        return;
    }
    ret = pci_add_capability(&s->pci.dev, PCI_CAP_ID_HT, 0,
                             AMDVI_CAPAB_REG_SIZE, errp);
    if (ret < 0) {
        return;
    }

    /* Pseudo address space under root PCI bus. */
    x86ms->ioapic_as = amdvi_host_dma_iommu(bus, s, AMDVI_IOAPIC_SB_DEVID);

    /* set up MMIO */
    memory_region_init_io(&s->mmio, OBJECT(s), &mmio_mem_ops, s, "amdvi-mmio",
                          AMDVI_MMIO_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
    sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, AMDVI_BASE_ADDR);
    pci_setup_iommu(bus, amdvi_host_dma_iommu, s);
    s->devid = object_property_get_int(OBJECT(&s->pci), "addr", &error_abort);
    msi_init(&s->pci.dev, 0, 1, true, false, errp);
    amdvi_init(s);
}

static const VMStateDescription vmstate_amdvi = {
    .name = "amd-iommu",
    .unmigratable = 1
};

static void amdvi_instance_init(Object *klass)
{
    AMDVIState *s = AMD_IOMMU_DEVICE(klass);

    object_initialize(&s->pci, sizeof(s->pci), TYPE_AMD_IOMMU_PCI);
}

static void amdvi_class_init(ObjectClass *klass, void* data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    X86IOMMUClass *dc_class = X86_IOMMU_CLASS(klass);

    dc->reset = amdvi_reset;
    dc->vmsd = &vmstate_amdvi;
    dc->hotpluggable = false;
    dc_class->realize = amdvi_realize;
    dc_class->int_remap = amdvi_int_remap;
    /* Supported by the pc-q35-* machine types */
    dc->user_creatable = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "AMD IOMMU (AMD-Vi) DMA Remapping device";
}

static const TypeInfo amdvi = {
    .name = TYPE_AMD_IOMMU_DEVICE,
    .parent = TYPE_X86_IOMMU_DEVICE,
    .instance_size = sizeof(AMDVIState),
    .instance_init = amdvi_instance_init,
    .class_init = amdvi_class_init
};

static const TypeInfo amdviPCI = {
    .name = "AMDVI-PCI",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(AMDVIPCIState),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void amdvi_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = amdvi_translate;
    imrc->notify_flag_changed = amdvi_iommu_notify_flag_changed;
}

static const TypeInfo amdvi_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_AMD_IOMMU_MEMORY_REGION,
    .class_init = amdvi_iommu_memory_region_class_init,
};

static void amdviPCI_register_types(void)
{
    type_register_static(&amdviPCI);
    type_register_static(&amdvi);
    type_register_static(&amdvi_iommu_memory_region_info);
}

type_init(amdviPCI_register_types);
