/*
 * Copyright (c) 2007, Neocleus Corporation.
 * Copyright (c) 2007, Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Alex Novik <alex@neocleus.com>
 * Allen Kay <allen.m.kay@intel.com>
 * Guy Zana <guy@neocleus.com>
 *
 * This file implements direct PCI assignment to a HVM guest
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/xen/xen-legacy-backend.h"
#include "xen_pt.h"

#define XEN_PT_MERGE_VALUE(value, data, val_mask) \
    (((value) & (val_mask)) | ((data) & ~(val_mask)))

#define XEN_PT_INVALID_REG          0xFFFFFFFF      /* invalid register value */

/* prototype */

static int xen_pt_ptr_reg_init(XenPCIPassthroughState *s, XenPTRegInfo *reg,
                               uint32_t real_offset, uint32_t *data);


/* helper */

/* A return value of 1 means the capability should NOT be exposed to guest. */
static int xen_pt_hide_dev_cap(const XenHostPCIDevice *d, uint8_t grp_id)
{
    switch (grp_id) {
    case PCI_CAP_ID_EXP:
        /* The PCI Express Capability Structure of the VF of Intel 82599 10GbE
         * Controller looks trivial, e.g., the PCI Express Capabilities
         * Register is 0. We should not try to expose it to guest.
         *
         * The datasheet is available at
         * http://download.intel.com/design/network/datashts/82599_datasheet.pdf
         *
         * See 'Table 9.7. VF PCIe Configuration Space' of the datasheet, the
         * PCI Express Capability Structure of the VF of Intel 82599 10GbE
         * Controller looks trivial, e.g., the PCI Express Capabilities
         * Register is 0, so the Capability Version is 0 and
         * xen_pt_pcie_size_init() would fail.
         */
        if (d->vendor_id == PCI_VENDOR_ID_INTEL &&
            d->device_id == PCI_DEVICE_ID_INTEL_82599_SFP_VF) {
            return 1;
        }
        break;
    }
    return 0;
}

/*   find emulate register group entry */
XenPTRegGroup *xen_pt_find_reg_grp(XenPCIPassthroughState *s, uint32_t address)
{
    XenPTRegGroup *entry = NULL;

    /* find register group entry */
    QLIST_FOREACH(entry, &s->reg_grps, entries) {
        /* check address */
        if ((entry->base_offset <= address)
            && ((entry->base_offset + entry->size) > address)) {
            return entry;
        }
    }

    /* group entry not found */
    return NULL;
}

/* find emulate register entry */
XenPTReg *xen_pt_find_reg(XenPTRegGroup *reg_grp, uint32_t address)
{
    XenPTReg *reg_entry = NULL;
    XenPTRegInfo *reg = NULL;
    uint32_t real_offset = 0;

    /* find register entry */
    QLIST_FOREACH(reg_entry, &reg_grp->reg_tbl_list, entries) {
        reg = reg_entry->reg;
        real_offset = reg_grp->base_offset + reg->offset;
        /* check address */
        if ((real_offset <= address)
            && ((real_offset + reg->size) > address)) {
            return reg_entry;
        }
    }

    return NULL;
}

static uint32_t get_throughable_mask(const XenPCIPassthroughState *s,
                                     XenPTRegInfo *reg, uint32_t valid_mask)
{
    uint32_t throughable_mask = ~(reg->emu_mask | reg->ro_mask);

    if (!s->permissive) {
        throughable_mask &= ~reg->res_mask;
    }

    return throughable_mask & valid_mask;
}

/****************
 * general register functions
 */

/* register initialization function */

static int xen_pt_common_reg_init(XenPCIPassthroughState *s,
                                  XenPTRegInfo *reg, uint32_t real_offset,
                                  uint32_t *data)
{
    *data = reg->init_val;
    return 0;
}

/* Read register functions */

static int xen_pt_byte_reg_read(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                uint8_t *value, uint8_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint8_t valid_emu_mask = 0;
    uint8_t *data = cfg_entry->ptr.byte;

    /* emulate byte register */
    valid_emu_mask = reg->emu_mask & valid_mask;
    *value = XEN_PT_MERGE_VALUE(*value, *data, ~valid_emu_mask);

    return 0;
}
static int xen_pt_word_reg_read(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                uint16_t *value, uint16_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint16_t valid_emu_mask = 0;
    uint16_t *data = cfg_entry->ptr.half_word;

    /* emulate word register */
    valid_emu_mask = reg->emu_mask & valid_mask;
    *value = XEN_PT_MERGE_VALUE(*value, *data, ~valid_emu_mask);

    return 0;
}
static int xen_pt_long_reg_read(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                uint32_t *value, uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint32_t valid_emu_mask = 0;
    uint32_t *data = cfg_entry->ptr.word;

    /* emulate long register */
    valid_emu_mask = reg->emu_mask & valid_mask;
    *value = XEN_PT_MERGE_VALUE(*value, *data, ~valid_emu_mask);

    return 0;
}

/* Write register functions */

static int xen_pt_byte_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                 uint8_t *val, uint8_t dev_value,
                                 uint8_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint8_t writable_mask = 0;
    uint8_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    uint8_t *data = cfg_entry->ptr.byte;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value & ~reg->rw1c_mask,
                              throughable_mask);

    return 0;
}
static int xen_pt_word_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                 uint16_t *val, uint16_t dev_value,
                                 uint16_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    uint16_t *data = cfg_entry->ptr.half_word;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value & ~reg->rw1c_mask,
                              throughable_mask);

    return 0;
}
static int xen_pt_long_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                 uint32_t *val, uint32_t dev_value,
                                 uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    uint32_t *data = cfg_entry->ptr.word;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value & ~reg->rw1c_mask,
                              throughable_mask);

    return 0;
}


/* XenPTRegInfo declaration
 * - only for emulated register (either a part or whole bit).
 * - for passthrough register that need special behavior (like interacting with
 *   other component), set emu_mask to all 0 and specify r/w func properly.
 * - do NOT use ALL F for init_val, otherwise the tbl will not be registered.
 */

/********************
 * Header Type0
 */

static int xen_pt_vendor_reg_init(XenPCIPassthroughState *s,
                                  XenPTRegInfo *reg, uint32_t real_offset,
                                  uint32_t *data)
{
    *data = s->real_device.vendor_id;
    return 0;
}
static int xen_pt_device_reg_init(XenPCIPassthroughState *s,
                                  XenPTRegInfo *reg, uint32_t real_offset,
                                  uint32_t *data)
{
    *data = s->real_device.device_id;
    return 0;
}
static int xen_pt_status_reg_init(XenPCIPassthroughState *s,
                                  XenPTRegInfo *reg, uint32_t real_offset,
                                  uint32_t *data)
{
    XenPTRegGroup *reg_grp_entry = NULL;
    XenPTReg *reg_entry = NULL;
    uint32_t reg_field = 0;

    /* find Header register group */
    reg_grp_entry = xen_pt_find_reg_grp(s, PCI_CAPABILITY_LIST);
    if (reg_grp_entry) {
        /* find Capabilities Pointer register */
        reg_entry = xen_pt_find_reg(reg_grp_entry, PCI_CAPABILITY_LIST);
        if (reg_entry) {
            /* check Capabilities Pointer register */
            if (*reg_entry->ptr.half_word) {
                reg_field |= PCI_STATUS_CAP_LIST;
            } else {
                reg_field &= ~PCI_STATUS_CAP_LIST;
            }
        } else {
            xen_shutdown_fatal_error("Internal error: Couldn't find XenPTReg*"
                                     " for Capabilities Pointer register."
                                     " (%s)\n", __func__);
            return -1;
        }
    } else {
        xen_shutdown_fatal_error("Internal error: Couldn't find XenPTRegGroup"
                                 " for Header. (%s)\n", __func__);
        return -1;
    }

    *data = reg_field;
    return 0;
}
static int xen_pt_header_type_reg_init(XenPCIPassthroughState *s,
                                       XenPTRegInfo *reg, uint32_t real_offset,
                                       uint32_t *data)
{
    /* read PCI_HEADER_TYPE */
    *data = reg->init_val | 0x80;
    return 0;
}

/* initialize Interrupt Pin register */
static int xen_pt_irqpin_reg_init(XenPCIPassthroughState *s,
                                  XenPTRegInfo *reg, uint32_t real_offset,
                                  uint32_t *data)
{
    if (s->real_device.irq) {
        *data = xen_pt_pci_read_intx(s);
    }
    return 0;
}

/* Command register */
static int xen_pt_cmd_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                uint16_t *val, uint16_t dev_value,
                                uint16_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    uint16_t *data = cfg_entry->ptr.half_word;

    /* modify emulate register */
    writable_mask = ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* create value for writing to I/O device register */
    if (*val & PCI_COMMAND_INTX_DISABLE) {
        throughable_mask |= PCI_COMMAND_INTX_DISABLE;
    } else {
        if (s->machine_irq) {
            throughable_mask |= PCI_COMMAND_INTX_DISABLE;
        }
    }

    *val = XEN_PT_MERGE_VALUE(*val, dev_value, throughable_mask);

    return 0;
}

/* BAR */
#define XEN_PT_BAR_MEM_RO_MASK    0x0000000F  /* BAR ReadOnly mask(Memory) */
#define XEN_PT_BAR_MEM_EMU_MASK   0xFFFFFFF0  /* BAR emul mask(Memory) */
#define XEN_PT_BAR_IO_RO_MASK     0x00000003  /* BAR ReadOnly mask(I/O) */
#define XEN_PT_BAR_IO_EMU_MASK    0xFFFFFFFC  /* BAR emul mask(I/O) */

static bool is_64bit_bar(PCIIORegion *r)
{
    return !!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64);
}

static uint64_t xen_pt_get_bar_size(PCIIORegion *r)
{
    if (is_64bit_bar(r)) {
        uint64_t size64;
        size64 = (r + 1)->size;
        size64 <<= 32;
        size64 += r->size;
        return size64;
    }
    return r->size;
}

static XenPTBarFlag xen_pt_bar_reg_parse(XenPCIPassthroughState *s,
                                         int index)
{
    PCIDevice *d = PCI_DEVICE(s);
    XenPTRegion *region = NULL;
    PCIIORegion *r;

    /* check 64bit BAR */
    if ((0 < index) && (index < PCI_ROM_SLOT)) {
        int type = s->real_device.io_regions[index - 1].type;

        if ((type & XEN_HOST_PCI_REGION_TYPE_MEM)
            && (type & XEN_HOST_PCI_REGION_TYPE_MEM_64)) {
            region = &s->bases[index - 1];
            if (region->bar_flag != XEN_PT_BAR_FLAG_UPPER) {
                return XEN_PT_BAR_FLAG_UPPER;
            }
        }
    }

    /* check unused BAR */
    r = &d->io_regions[index];
    if (!xen_pt_get_bar_size(r)) {
        return XEN_PT_BAR_FLAG_UNUSED;
    }

    /* for ExpROM BAR */
    if (index == PCI_ROM_SLOT) {
        return XEN_PT_BAR_FLAG_MEM;
    }

    /* check BAR I/O indicator */
    if (s->real_device.io_regions[index].type & XEN_HOST_PCI_REGION_TYPE_IO) {
        return XEN_PT_BAR_FLAG_IO;
    } else {
        return XEN_PT_BAR_FLAG_MEM;
    }
}

static inline uint32_t base_address_with_flags(XenHostPCIIORegion *hr)
{
    if (hr->type & XEN_HOST_PCI_REGION_TYPE_IO) {
        return hr->base_addr | (hr->bus_flags & ~PCI_BASE_ADDRESS_IO_MASK);
    } else {
        return hr->base_addr | (hr->bus_flags & ~PCI_BASE_ADDRESS_MEM_MASK);
    }
}

static int xen_pt_bar_reg_init(XenPCIPassthroughState *s, XenPTRegInfo *reg,
                               uint32_t real_offset, uint32_t *data)
{
    uint32_t reg_field = 0;
    int index;

    index = xen_pt_bar_offset_to_index(reg->offset);
    if (index < 0 || index >= PCI_NUM_REGIONS) {
        XEN_PT_ERR(&s->dev, "Internal error: Invalid BAR index [%d].\n", index);
        return -1;
    }

    /* set BAR flag */
    s->bases[index].bar_flag = xen_pt_bar_reg_parse(s, index);
    if (s->bases[index].bar_flag == XEN_PT_BAR_FLAG_UNUSED) {
        reg_field = XEN_PT_INVALID_REG;
    }

    *data = reg_field;
    return 0;
}
static int xen_pt_bar_reg_read(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                               uint32_t *value, uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint32_t valid_emu_mask = 0;
    uint32_t bar_emu_mask = 0;
    int index;

    /* get BAR index */
    index = xen_pt_bar_offset_to_index(reg->offset);
    if (index < 0 || index >= PCI_NUM_REGIONS - 1) {
        XEN_PT_ERR(&s->dev, "Internal error: Invalid BAR index [%d].\n", index);
        return -1;
    }

    /* use fixed-up value from kernel sysfs */
    *value = base_address_with_flags(&s->real_device.io_regions[index]);

    /* set emulate mask depend on BAR flag */
    switch (s->bases[index].bar_flag) {
    case XEN_PT_BAR_FLAG_MEM:
        bar_emu_mask = XEN_PT_BAR_MEM_EMU_MASK;
        break;
    case XEN_PT_BAR_FLAG_IO:
        bar_emu_mask = XEN_PT_BAR_IO_EMU_MASK;
        break;
    case XEN_PT_BAR_FLAG_UPPER:
        bar_emu_mask = XEN_PT_BAR_ALLF;
        break;
    default:
        break;
    }

    /* emulate BAR */
    valid_emu_mask = bar_emu_mask & valid_mask;
    *value = XEN_PT_MERGE_VALUE(*value, *cfg_entry->ptr.word, ~valid_emu_mask);

    return 0;
}
static int xen_pt_bar_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                uint32_t *val, uint32_t dev_value,
                                uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    XenPTRegion *base = NULL;
    PCIDevice *d = PCI_DEVICE(s);
    const PCIIORegion *r;
    uint32_t writable_mask = 0;
    uint32_t bar_emu_mask = 0;
    uint32_t bar_ro_mask = 0;
    uint32_t r_size = 0;
    int index = 0;
    uint32_t *data = cfg_entry->ptr.word;

    index = xen_pt_bar_offset_to_index(reg->offset);
    if (index < 0 || index >= PCI_NUM_REGIONS) {
        XEN_PT_ERR(d, "Internal error: Invalid BAR index [%d].\n", index);
        return -1;
    }

    r = &d->io_regions[index];
    base = &s->bases[index];
    r_size = xen_pt_get_emul_size(base->bar_flag, r->size);

    /* set emulate mask and read-only mask values depend on the BAR flag */
    switch (s->bases[index].bar_flag) {
    case XEN_PT_BAR_FLAG_MEM:
        bar_emu_mask = XEN_PT_BAR_MEM_EMU_MASK;
        if (!r_size) {
            /* low 32 bits mask for 64 bit bars */
            bar_ro_mask = XEN_PT_BAR_ALLF;
        } else {
            bar_ro_mask = XEN_PT_BAR_MEM_RO_MASK | (r_size - 1);
        }
        break;
    case XEN_PT_BAR_FLAG_IO:
        bar_emu_mask = XEN_PT_BAR_IO_EMU_MASK;
        bar_ro_mask = XEN_PT_BAR_IO_RO_MASK | (r_size - 1);
        break;
    case XEN_PT_BAR_FLAG_UPPER:
        assert(index > 0);
        r_size = d->io_regions[index - 1].size >> 32;
        bar_emu_mask = XEN_PT_BAR_ALLF;
        bar_ro_mask = r_size ? r_size - 1 : 0;
        break;
    default:
        break;
    }

    /* modify emulate register */
    writable_mask = bar_emu_mask & ~bar_ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* check whether we need to update the virtual region address or not */
    switch (s->bases[index].bar_flag) {
    case XEN_PT_BAR_FLAG_UPPER:
    case XEN_PT_BAR_FLAG_MEM:
        /* nothing to do */
        break;
    case XEN_PT_BAR_FLAG_IO:
        /* nothing to do */
        break;
    default:
        break;
    }

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, 0);

    return 0;
}

/* write Exp ROM BAR */
static int xen_pt_exp_rom_bar_reg_write(XenPCIPassthroughState *s,
                                        XenPTReg *cfg_entry, uint32_t *val,
                                        uint32_t dev_value, uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    XenPTRegion *base = NULL;
    PCIDevice *d = PCI_DEVICE(s);
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    pcibus_t r_size = 0;
    uint32_t bar_ro_mask = 0;
    uint32_t *data = cfg_entry->ptr.word;

    r_size = d->io_regions[PCI_ROM_SLOT].size;
    base = &s->bases[PCI_ROM_SLOT];
    /* align memory type resource size */
    r_size = xen_pt_get_emul_size(base->bar_flag, r_size);

    /* set emulate mask and read-only mask */
    bar_ro_mask = (reg->ro_mask | (r_size - 1)) & ~PCI_ROM_ADDRESS_ENABLE;

    /* modify emulate register */
    writable_mask = ~bar_ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, throughable_mask);

    return 0;
}

static int xen_pt_intel_opregion_read(XenPCIPassthroughState *s,
                                      XenPTReg *cfg_entry,
                                      uint32_t *value, uint32_t valid_mask)
{
    *value = igd_read_opregion(s);
    return 0;
}

static int xen_pt_intel_opregion_write(XenPCIPassthroughState *s,
                                       XenPTReg *cfg_entry, uint32_t *value,
                                       uint32_t dev_value, uint32_t valid_mask)
{
    igd_write_opregion(s, *value);
    return 0;
}

/* Header Type0 reg static information table */
static XenPTRegInfo xen_pt_emu_reg_header0[] = {
    /* Vendor ID reg */
    {
        .offset     = PCI_VENDOR_ID,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFFF,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_vendor_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Device ID reg */
    {
        .offset     = PCI_DEVICE_ID,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFFF,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_device_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Command reg */
    {
        .offset     = PCI_COMMAND,
        .size       = 2,
        .init_val   = 0x0000,
        .res_mask   = 0xF880,
        .emu_mask   = 0x0743,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_cmd_reg_write,
    },
    /* Capabilities Pointer reg */
    {
        .offset     = PCI_CAPABILITY_LIST,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Status reg */
    /* use emulated Cap Ptr value to initialize,
     * so need to be declared after Cap Ptr reg
     */
    {
        .offset     = PCI_STATUS,
        .size       = 2,
        .init_val   = 0x0000,
        .res_mask   = 0x0007,
        .ro_mask    = 0x06F8,
        .rw1c_mask  = 0xF900,
        .emu_mask   = 0x0010,
        .init       = xen_pt_status_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Cache Line Size reg */
    {
        .offset     = PCI_CACHE_LINE_SIZE,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0x00,
        .emu_mask   = 0xFF,
        .init       = xen_pt_common_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Latency Timer reg */
    {
        .offset     = PCI_LATENCY_TIMER,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0x00,
        .emu_mask   = 0xFF,
        .init       = xen_pt_common_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Header Type reg */
    {
        .offset     = PCI_HEADER_TYPE,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0x00,
        .init       = xen_pt_header_type_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Interrupt Line reg */
    {
        .offset     = PCI_INTERRUPT_LINE,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0x00,
        .emu_mask   = 0xFF,
        .init       = xen_pt_common_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Interrupt Pin reg */
    {
        .offset     = PCI_INTERRUPT_PIN,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_irqpin_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* BAR 0 reg */
    /* mask of BAR need to be decided later, depends on IO/MEM type */
    {
        .offset     = PCI_BASE_ADDRESS_0,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_bar_reg_read,
        .u.dw.write = xen_pt_bar_reg_write,
    },
    /* BAR 1 reg */
    {
        .offset     = PCI_BASE_ADDRESS_1,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_bar_reg_read,
        .u.dw.write = xen_pt_bar_reg_write,
    },
    /* BAR 2 reg */
    {
        .offset     = PCI_BASE_ADDRESS_2,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_bar_reg_read,
        .u.dw.write = xen_pt_bar_reg_write,
    },
    /* BAR 3 reg */
    {
        .offset     = PCI_BASE_ADDRESS_3,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_bar_reg_read,
        .u.dw.write = xen_pt_bar_reg_write,
    },
    /* BAR 4 reg */
    {
        .offset     = PCI_BASE_ADDRESS_4,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_bar_reg_read,
        .u.dw.write = xen_pt_bar_reg_write,
    },
    /* BAR 5 reg */
    {
        .offset     = PCI_BASE_ADDRESS_5,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_bar_reg_read,
        .u.dw.write = xen_pt_bar_reg_write,
    },
    /* Expansion ROM BAR reg */
    {
        .offset     = PCI_ROM_ADDRESS,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = ~PCI_ROM_ADDRESS_MASK & ~PCI_ROM_ADDRESS_ENABLE,
        .emu_mask   = (uint32_t)PCI_ROM_ADDRESS_MASK,
        .init       = xen_pt_bar_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_exp_rom_bar_reg_write,
    },
    {
        .size = 0,
    },
};


/*********************************
 * Vital Product Data Capability
 */

/* Vital Product Data Capability Structure reg static information table */
static XenPTRegInfo xen_pt_emu_reg_vpd[] = {
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    {
        .offset     = PCI_VPD_ADDR,
        .size       = 2,
        .ro_mask    = 0x0003,
        .emu_mask   = 0x0003,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    {
        .size = 0,
    },
};


/**************************************
 * Vendor Specific Capability
 */

/* Vendor Specific Capability Structure reg static information table */
static XenPTRegInfo xen_pt_emu_reg_vendor[] = {
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    {
        .size = 0,
    },
};


/*****************************
 * PCI Express Capability
 */

static inline uint8_t get_capability_version(XenPCIPassthroughState *s,
                                             uint32_t offset)
{
    uint8_t flag;
    if (xen_host_pci_get_byte(&s->real_device, offset + PCI_EXP_FLAGS, &flag)) {
        return 0;
    }
    return flag & PCI_EXP_FLAGS_VERS;
}

static inline uint8_t get_device_type(XenPCIPassthroughState *s,
                                      uint32_t offset)
{
    uint8_t flag;
    if (xen_host_pci_get_byte(&s->real_device, offset + PCI_EXP_FLAGS, &flag)) {
        return 0;
    }
    return (flag & PCI_EXP_FLAGS_TYPE) >> 4;
}

/* initialize Link Control register */
static int xen_pt_linkctrl_reg_init(XenPCIPassthroughState *s,
                                    XenPTRegInfo *reg, uint32_t real_offset,
                                    uint32_t *data)
{
    uint8_t cap_ver = get_capability_version(s, real_offset - reg->offset);
    uint8_t dev_type = get_device_type(s, real_offset - reg->offset);

    /* no need to initialize in case of Root Complex Integrated Endpoint
     * with cap_ver 1.x
     */
    if ((dev_type == PCI_EXP_TYPE_RC_END) && (cap_ver == 1)) {
        *data = XEN_PT_INVALID_REG;
    }

    *data = reg->init_val;
    return 0;
}
/* initialize Device Control 2 register */
static int xen_pt_devctrl2_reg_init(XenPCIPassthroughState *s,
                                    XenPTRegInfo *reg, uint32_t real_offset,
                                    uint32_t *data)
{
    uint8_t cap_ver = get_capability_version(s, real_offset - reg->offset);

    /* no need to initialize in case of cap_ver 1.x */
    if (cap_ver == 1) {
        *data = XEN_PT_INVALID_REG;
    }

    *data = reg->init_val;
    return 0;
}
/* initialize Link Control 2 register */
static int xen_pt_linkctrl2_reg_init(XenPCIPassthroughState *s,
                                     XenPTRegInfo *reg, uint32_t real_offset,
                                     uint32_t *data)
{
    uint8_t cap_ver = get_capability_version(s, real_offset - reg->offset);
    uint32_t reg_field = 0;

    /* no need to initialize in case of cap_ver 1.x */
    if (cap_ver == 1) {
        reg_field = XEN_PT_INVALID_REG;
    } else {
        /* set Supported Link Speed */
        uint8_t lnkcap;
        int rc;
        rc = xen_host_pci_get_byte(&s->real_device,
                                   real_offset - reg->offset + PCI_EXP_LNKCAP,
                                   &lnkcap);
        if (rc) {
            return rc;
        }
        reg_field |= PCI_EXP_LNKCAP_SLS & lnkcap;
    }

    *data = reg_field;
    return 0;
}

/* PCI Express Capability Structure reg static information table */
static XenPTRegInfo xen_pt_emu_reg_pcie[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Device Capabilities reg */
    {
        .offset     = PCI_EXP_DEVCAP,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0xFFFFFFFF,
        .emu_mask   = 0x10000000,
        .init       = xen_pt_common_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_long_reg_write,
    },
    /* Device Control reg */
    {
        .offset     = PCI_EXP_DEVCTL,
        .size       = 2,
        .init_val   = 0x2810,
        .ro_mask    = 0x8400,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Device Status reg */
    {
        .offset     = PCI_EXP_DEVSTA,
        .size       = 2,
        .res_mask   = 0xFFC0,
        .ro_mask    = 0x0030,
        .rw1c_mask  = 0x000F,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Link Control reg */
    {
        .offset     = PCI_EXP_LNKCTL,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFC34,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_linkctrl_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Link Status reg */
    {
        .offset     = PCI_EXP_LNKSTA,
        .size       = 2,
        .ro_mask    = 0x3FFF,
        .rw1c_mask  = 0xC000,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Device Control 2 reg */
    {
        .offset     = 0x28,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFE0,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_devctrl2_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* Link Control 2 reg */
    {
        .offset     = 0x30,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xE040,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_linkctrl2_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    {
        .size = 0,
    },
};


/*********************************
 * Power Management Capability
 */

/* Power Management Capability reg static information table */
static XenPTRegInfo xen_pt_emu_reg_pm[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Power Management Capabilities reg */
    {
        .offset     = PCI_CAP_FLAGS,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFFF,
        .emu_mask   = 0xF9C8,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    /* PCI Power Management Control/Status reg */
    {
        .offset     = PCI_PM_CTRL,
        .size       = 2,
        .init_val   = 0x0008,
        .res_mask   = 0x00F0,
        .ro_mask    = 0x610C,
        .rw1c_mask  = 0x8000,
        .emu_mask   = 0x810B,
        .init       = xen_pt_common_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_word_reg_write,
    },
    {
        .size = 0,
    },
};


/********************************
 * MSI Capability
 */

/* Helper */
#define xen_pt_msi_check_type(offset, flags, what) \
        ((offset) == ((flags) & PCI_MSI_FLAGS_64BIT ? \
                      PCI_MSI_##what##_64 : PCI_MSI_##what##_32))

/* Message Control register */
static int xen_pt_msgctrl_reg_init(XenPCIPassthroughState *s,
                                   XenPTRegInfo *reg, uint32_t real_offset,
                                   uint32_t *data)
{
    XenPTMSI *msi = s->msi;
    uint16_t reg_field;
    int rc;

    /* use I/O device register's value as initial value */
    rc = xen_host_pci_get_word(&s->real_device, real_offset, &reg_field);
    if (rc) {
        return rc;
    }
    if (reg_field & PCI_MSI_FLAGS_ENABLE) {
        XEN_PT_LOG(&s->dev, "MSI already enabled, disabling it first\n");
        xen_host_pci_set_word(&s->real_device, real_offset,
                              reg_field & ~PCI_MSI_FLAGS_ENABLE);
    }
    msi->flags |= reg_field;
    msi->ctrl_offset = real_offset;
    msi->initialized = false;
    msi->mapped = false;

    *data = reg->init_val;
    return 0;
}
static int xen_pt_msgctrl_reg_write(XenPCIPassthroughState *s,
                                    XenPTReg *cfg_entry, uint16_t *val,
                                    uint16_t dev_value, uint16_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    XenPTMSI *msi = s->msi;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    uint16_t *data = cfg_entry->ptr.half_word;

    /* Currently no support for multi-vector */
    if (*val & PCI_MSI_FLAGS_QSIZE) {
        XEN_PT_WARN(&s->dev, "Tries to set more than 1 vector ctrl %x\n", *val);
    }

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);
    msi->flags |= *data & ~PCI_MSI_FLAGS_ENABLE;

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, throughable_mask);

    /* update MSI */
    if (*val & PCI_MSI_FLAGS_ENABLE) {
        /* setup MSI pirq for the first time */
        if (!msi->initialized) {
            /* Init physical one */
            XEN_PT_LOG(&s->dev, "setup MSI (register: %x).\n", *val);
            if (xen_pt_msi_setup(s)) {
                /* We do not broadcast the error to the framework code, so
                 * that MSI errors are contained in MSI emulation code and
                 * QEMU can go on running.
                 * Guest MSI would be actually not working.
                 */
                *val &= ~PCI_MSI_FLAGS_ENABLE;
                XEN_PT_WARN(&s->dev, "Can not map MSI (register: %x)!\n", *val);
                return 0;
            }
            if (xen_pt_msi_update(s)) {
                *val &= ~PCI_MSI_FLAGS_ENABLE;
                XEN_PT_WARN(&s->dev, "Can not bind MSI (register: %x)!\n", *val);
                return 0;
            }
            msi->initialized = true;
            msi->mapped = true;
        }
        msi->flags |= PCI_MSI_FLAGS_ENABLE;
    } else if (msi->mapped) {
        xen_pt_msi_disable(s);
    }

    return 0;
}

/* initialize Message Upper Address register */
static int xen_pt_msgaddr64_reg_init(XenPCIPassthroughState *s,
                                     XenPTRegInfo *reg, uint32_t real_offset,
                                     uint32_t *data)
{
    /* no need to initialize in case of 32 bit type */
    if (!(s->msi->flags & PCI_MSI_FLAGS_64BIT)) {
        *data = XEN_PT_INVALID_REG;
    } else {
        *data = reg->init_val;
    }

    return 0;
}
/* this function will be called twice (for 32 bit and 64 bit type) */
/* initialize Message Data register */
static int xen_pt_msgdata_reg_init(XenPCIPassthroughState *s,
                                   XenPTRegInfo *reg, uint32_t real_offset,
                                   uint32_t *data)
{
    uint32_t flags = s->msi->flags;
    uint32_t offset = reg->offset;

    /* check the offset whether matches the type or not */
    if (xen_pt_msi_check_type(offset, flags, DATA)) {
        *data = reg->init_val;
    } else {
        *data = XEN_PT_INVALID_REG;
    }
    return 0;
}

/* this function will be called twice (for 32 bit and 64 bit type) */
/* initialize Mask register */
static int xen_pt_mask_reg_init(XenPCIPassthroughState *s,
                                XenPTRegInfo *reg, uint32_t real_offset,
                                uint32_t *data)
{
    uint32_t flags = s->msi->flags;

    /* check the offset whether matches the type or not */
    if (!(flags & PCI_MSI_FLAGS_MASKBIT)) {
        *data = XEN_PT_INVALID_REG;
    } else if (xen_pt_msi_check_type(reg->offset, flags, MASK)) {
        *data = reg->init_val;
    } else {
        *data = XEN_PT_INVALID_REG;
    }
    return 0;
}

/* this function will be called twice (for 32 bit and 64 bit type) */
/* initialize Pending register */
static int xen_pt_pending_reg_init(XenPCIPassthroughState *s,
                                   XenPTRegInfo *reg, uint32_t real_offset,
                                   uint32_t *data)
{
    uint32_t flags = s->msi->flags;

    /* check the offset whether matches the type or not */
    if (!(flags & PCI_MSI_FLAGS_MASKBIT)) {
        *data = XEN_PT_INVALID_REG;
    } else if (xen_pt_msi_check_type(reg->offset, flags, PENDING)) {
        *data = reg->init_val;
    } else {
        *data = XEN_PT_INVALID_REG;
    }
    return 0;
}

/* write Message Address register */
static int xen_pt_msgaddr32_reg_write(XenPCIPassthroughState *s,
                                      XenPTReg *cfg_entry, uint32_t *val,
                                      uint32_t dev_value, uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint32_t writable_mask = 0;
    uint32_t old_addr = *cfg_entry->ptr.word;
    uint32_t *data = cfg_entry->ptr.word;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);
    s->msi->addr_lo = *data;

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, 0);

    /* update MSI */
    if (*data != old_addr) {
        if (s->msi->mapped) {
            xen_pt_msi_update(s);
        }
    }

    return 0;
}
/* write Message Upper Address register */
static int xen_pt_msgaddr64_reg_write(XenPCIPassthroughState *s,
                                      XenPTReg *cfg_entry, uint32_t *val,
                                      uint32_t dev_value, uint32_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint32_t writable_mask = 0;
    uint32_t old_addr = *cfg_entry->ptr.word;
    uint32_t *data = cfg_entry->ptr.word;

    /* check whether the type is 64 bit or not */
    if (!(s->msi->flags & PCI_MSI_FLAGS_64BIT)) {
        XEN_PT_ERR(&s->dev,
                   "Can't write to the upper address without 64 bit support\n");
        return -1;
    }

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);
    /* update the msi_info too */
    s->msi->addr_hi = *data;

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, 0);

    /* update MSI */
    if (*data != old_addr) {
        if (s->msi->mapped) {
            xen_pt_msi_update(s);
        }
    }

    return 0;
}


/* this function will be called twice (for 32 bit and 64 bit type) */
/* write Message Data register */
static int xen_pt_msgdata_reg_write(XenPCIPassthroughState *s,
                                    XenPTReg *cfg_entry, uint16_t *val,
                                    uint16_t dev_value, uint16_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    XenPTMSI *msi = s->msi;
    uint16_t writable_mask = 0;
    uint16_t old_data = *cfg_entry->ptr.half_word;
    uint32_t offset = reg->offset;
    uint16_t *data = cfg_entry->ptr.half_word;

    /* check the offset whether matches the type or not */
    if (!xen_pt_msi_check_type(offset, msi->flags, DATA)) {
        /* exit I/O emulator */
        XEN_PT_ERR(&s->dev, "the offset does not match the 32/64 bit type!\n");
        return -1;
    }

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);
    /* update the msi_info too */
    msi->data = *data;

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, 0);

    /* update MSI */
    if (*data != old_data) {
        if (msi->mapped) {
            xen_pt_msi_update(s);
        }
    }

    return 0;
}

static int xen_pt_mask_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                 uint32_t *val, uint32_t dev_value,
                                 uint32_t valid_mask)
{
    int rc;

    rc = xen_pt_long_reg_write(s, cfg_entry, val, dev_value, valid_mask);
    if (rc) {
        return rc;
    }

    s->msi->mask = *val;

    return 0;
}

/* MSI Capability Structure reg static information table */
static XenPTRegInfo xen_pt_emu_reg_msi[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Message Control reg */
    {
        .offset     = PCI_MSI_FLAGS,
        .size       = 2,
        .init_val   = 0x0000,
        .res_mask   = 0xFE00,
        .ro_mask    = 0x018E,
        .emu_mask   = 0x017E,
        .init       = xen_pt_msgctrl_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_msgctrl_reg_write,
    },
    /* Message Address reg */
    {
        .offset     = PCI_MSI_ADDRESS_LO,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0x00000003,
        .emu_mask   = 0xFFFFFFFF,
        .init       = xen_pt_common_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_msgaddr32_reg_write,
    },
    /* Message Upper Address reg (if PCI_MSI_FLAGS_64BIT set) */
    {
        .offset     = PCI_MSI_ADDRESS_HI,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0x00000000,
        .emu_mask   = 0xFFFFFFFF,
        .init       = xen_pt_msgaddr64_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_msgaddr64_reg_write,
    },
    /* Message Data reg (16 bits of data for 32-bit devices) */
    {
        .offset     = PCI_MSI_DATA_32,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0x0000,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_msgdata_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_msgdata_reg_write,
    },
    /* Message Data reg (16 bits of data for 64-bit devices) */
    {
        .offset     = PCI_MSI_DATA_64,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0x0000,
        .emu_mask   = 0xFFFF,
        .init       = xen_pt_msgdata_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_msgdata_reg_write,
    },
    /* Mask reg (if PCI_MSI_FLAGS_MASKBIT set, for 32-bit devices) */
    {
        .offset     = PCI_MSI_MASK_32,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0xFFFFFFFF,
        .emu_mask   = 0xFFFFFFFF,
        .init       = xen_pt_mask_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_mask_reg_write,
    },
    /* Mask reg (if PCI_MSI_FLAGS_MASKBIT set, for 64-bit devices) */
    {
        .offset     = PCI_MSI_MASK_64,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0xFFFFFFFF,
        .emu_mask   = 0xFFFFFFFF,
        .init       = xen_pt_mask_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_mask_reg_write,
    },
    /* Pending reg (if PCI_MSI_FLAGS_MASKBIT set, for 32-bit devices) */
    {
        .offset     = PCI_MSI_MASK_32 + 4,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0xFFFFFFFF,
        .emu_mask   = 0x00000000,
        .init       = xen_pt_pending_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_long_reg_write,
    },
    /* Pending reg (if PCI_MSI_FLAGS_MASKBIT set, for 64-bit devices) */
    {
        .offset     = PCI_MSI_MASK_64 + 4,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0xFFFFFFFF,
        .emu_mask   = 0x00000000,
        .init       = xen_pt_pending_reg_init,
        .u.dw.read  = xen_pt_long_reg_read,
        .u.dw.write = xen_pt_long_reg_write,
    },
    {
        .size = 0,
    },
};


/**************************************
 * MSI-X Capability
 */

/* Message Control register for MSI-X */
static int xen_pt_msixctrl_reg_init(XenPCIPassthroughState *s,
                                    XenPTRegInfo *reg, uint32_t real_offset,
                                    uint32_t *data)
{
    uint16_t reg_field;
    int rc;

    /* use I/O device register's value as initial value */
    rc = xen_host_pci_get_word(&s->real_device, real_offset, &reg_field);
    if (rc) {
        return rc;
    }
    if (reg_field & PCI_MSIX_FLAGS_ENABLE) {
        XEN_PT_LOG(&s->dev, "MSIX already enabled, disabling it first\n");
        xen_host_pci_set_word(&s->real_device, real_offset,
                              reg_field & ~PCI_MSIX_FLAGS_ENABLE);
    }

    s->msix->ctrl_offset = real_offset;

    *data = reg->init_val;
    return 0;
}
static int xen_pt_msixctrl_reg_write(XenPCIPassthroughState *s,
                                     XenPTReg *cfg_entry, uint16_t *val,
                                     uint16_t dev_value, uint16_t valid_mask)
{
    XenPTRegInfo *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = get_throughable_mask(s, reg, valid_mask);
    int debug_msix_enabled_old;
    uint16_t *data = cfg_entry->ptr.half_word;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    *data = XEN_PT_MERGE_VALUE(*val, *data, writable_mask);

    /* create value for writing to I/O device register */
    *val = XEN_PT_MERGE_VALUE(*val, dev_value, throughable_mask);

    /* update MSI-X */
    if ((*val & PCI_MSIX_FLAGS_ENABLE)
        && !(*val & PCI_MSIX_FLAGS_MASKALL)) {
        xen_pt_msix_update(s);
    } else if (!(*val & PCI_MSIX_FLAGS_ENABLE) && s->msix->enabled) {
        xen_pt_msix_disable(s);
    }

    s->msix->maskall = *val & PCI_MSIX_FLAGS_MASKALL;

    debug_msix_enabled_old = s->msix->enabled;
    s->msix->enabled = !!(*val & PCI_MSIX_FLAGS_ENABLE);
    if (s->msix->enabled != debug_msix_enabled_old) {
        XEN_PT_LOG(&s->dev, "%s MSI-X\n",
                   s->msix->enabled ? "enable" : "disable");
    }

    return 0;
}

/* MSI-X Capability Structure reg static information table */
static XenPTRegInfo xen_pt_emu_reg_msix[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = xen_pt_ptr_reg_init,
        .u.b.read   = xen_pt_byte_reg_read,
        .u.b.write  = xen_pt_byte_reg_write,
    },
    /* Message Control reg */
    {
        .offset     = PCI_MSI_FLAGS,
        .size       = 2,
        .init_val   = 0x0000,
        .res_mask   = 0x3800,
        .ro_mask    = 0x07FF,
        .emu_mask   = 0x0000,
        .init       = xen_pt_msixctrl_reg_init,
        .u.w.read   = xen_pt_word_reg_read,
        .u.w.write  = xen_pt_msixctrl_reg_write,
    },
    {
        .size = 0,
    },
};

static XenPTRegInfo xen_pt_emu_reg_igd_opregion[] = {
    /* Intel IGFX OpRegion reg */
    {
        .offset     = 0x0,
        .size       = 4,
        .init_val   = 0,
        .emu_mask   = 0xFFFFFFFF,
        .u.dw.read   = xen_pt_intel_opregion_read,
        .u.dw.write  = xen_pt_intel_opregion_write,
    },
    {
        .size = 0,
    },
};

/****************************
 * Capabilities
 */

/* capability structure register group size functions */

static int xen_pt_reg_grp_size_init(XenPCIPassthroughState *s,
                                    const XenPTRegGroupInfo *grp_reg,
                                    uint32_t base_offset, uint8_t *size)
{
    *size = grp_reg->grp_size;
    return 0;
}
/* get Vendor Specific Capability Structure register group size */
static int xen_pt_vendor_size_init(XenPCIPassthroughState *s,
                                   const XenPTRegGroupInfo *grp_reg,
                                   uint32_t base_offset, uint8_t *size)
{
    return xen_host_pci_get_byte(&s->real_device, base_offset + 0x02, size);
}
/* get PCI Express Capability Structure register group size */
static int xen_pt_pcie_size_init(XenPCIPassthroughState *s,
                                 const XenPTRegGroupInfo *grp_reg,
                                 uint32_t base_offset, uint8_t *size)
{
    PCIDevice *d = PCI_DEVICE(s);
    uint8_t version = get_capability_version(s, base_offset);
    uint8_t type = get_device_type(s, base_offset);
    uint8_t pcie_size = 0;


    /* calculate size depend on capability version and device/port type */
    /* in case of PCI Express Base Specification Rev 1.x */
    if (version == 1) {
        /* The PCI Express Capabilities, Device Capabilities, and Device
         * Status/Control registers are required for all PCI Express devices.
         * The Link Capabilities and Link Status/Control are required for all
         * Endpoints that are not Root Complex Integrated Endpoints. Endpoints
         * are not required to implement registers other than those listed
         * above and terminate the capability structure.
         */
        switch (type) {
        case PCI_EXP_TYPE_ENDPOINT:
        case PCI_EXP_TYPE_LEG_END:
            pcie_size = 0x14;
            break;
        case PCI_EXP_TYPE_RC_END:
            /* has no link */
            pcie_size = 0x0C;
            break;
            /* only EndPoint passthrough is supported */
        case PCI_EXP_TYPE_ROOT_PORT:
        case PCI_EXP_TYPE_UPSTREAM:
        case PCI_EXP_TYPE_DOWNSTREAM:
        case PCI_EXP_TYPE_PCI_BRIDGE:
        case PCI_EXP_TYPE_PCIE_BRIDGE:
        case PCI_EXP_TYPE_RC_EC:
        default:
            XEN_PT_ERR(d, "Unsupported device/port type %#x.\n", type);
            return -1;
        }
    }
    /* in case of PCI Express Base Specification Rev 2.0 */
    else if (version == 2) {
        switch (type) {
        case PCI_EXP_TYPE_ENDPOINT:
        case PCI_EXP_TYPE_LEG_END:
        case PCI_EXP_TYPE_RC_END:
            /* For Functions that do not implement the registers,
             * these spaces must be hardwired to 0b.
             */
            pcie_size = 0x3C;
            break;
            /* only EndPoint passthrough is supported */
        case PCI_EXP_TYPE_ROOT_PORT:
        case PCI_EXP_TYPE_UPSTREAM:
        case PCI_EXP_TYPE_DOWNSTREAM:
        case PCI_EXP_TYPE_PCI_BRIDGE:
        case PCI_EXP_TYPE_PCIE_BRIDGE:
        case PCI_EXP_TYPE_RC_EC:
        default:
            XEN_PT_ERR(d, "Unsupported device/port type %#x.\n", type);
            return -1;
        }
    } else {
        XEN_PT_ERR(d, "Unsupported capability version %#x.\n", version);
        return -1;
    }

    *size = pcie_size;
    return 0;
}
/* get MSI Capability Structure register group size */
static int xen_pt_msi_size_init(XenPCIPassthroughState *s,
                                const XenPTRegGroupInfo *grp_reg,
                                uint32_t base_offset, uint8_t *size)
{
    uint16_t msg_ctrl = 0;
    uint8_t msi_size = 0xa;
    int rc;

    rc = xen_host_pci_get_word(&s->real_device, base_offset + PCI_MSI_FLAGS,
                               &msg_ctrl);
    if (rc) {
        return rc;
    }
    /* check if 64-bit address is capable of per-vector masking */
    if (msg_ctrl & PCI_MSI_FLAGS_64BIT) {
        msi_size += 4;
    }
    if (msg_ctrl & PCI_MSI_FLAGS_MASKBIT) {
        msi_size += 10;
    }

    s->msi = g_new0(XenPTMSI, 1);
    s->msi->pirq = XEN_PT_UNASSIGNED_PIRQ;

    *size = msi_size;
    return 0;
}
/* get MSI-X Capability Structure register group size */
static int xen_pt_msix_size_init(XenPCIPassthroughState *s,
                                 const XenPTRegGroupInfo *grp_reg,
                                 uint32_t base_offset, uint8_t *size)
{
    int rc = 0;

    rc = xen_pt_msix_init(s, base_offset);

    if (rc < 0) {
        XEN_PT_ERR(&s->dev, "Internal error: Invalid xen_pt_msix_init.\n");
        return rc;
    }

    *size = grp_reg->grp_size;
    return 0;
}


static const XenPTRegGroupInfo xen_pt_emu_reg_grps[] = {
    /* Header Type0 reg group */
    {
        .grp_id      = 0xFF,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0x40,
        .size_init   = xen_pt_reg_grp_size_init,
        .emu_regs = xen_pt_emu_reg_header0,
    },
    /* PCI PowerManagement Capability reg group */
    {
        .grp_id      = PCI_CAP_ID_PM,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = PCI_PM_SIZEOF,
        .size_init   = xen_pt_reg_grp_size_init,
        .emu_regs = xen_pt_emu_reg_pm,
    },
    /* AGP Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_AGP,
        .grp_type   = XEN_PT_GRP_TYPE_HARDWIRED,
        .grp_size   = 0x30,
        .size_init  = xen_pt_reg_grp_size_init,
    },
    /* Vital Product Data Capability Structure reg group */
    {
        .grp_id      = PCI_CAP_ID_VPD,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0x08,
        .size_init   = xen_pt_reg_grp_size_init,
        .emu_regs = xen_pt_emu_reg_vpd,
    },
    /* Slot Identification reg group */
    {
        .grp_id     = PCI_CAP_ID_SLOTID,
        .grp_type   = XEN_PT_GRP_TYPE_HARDWIRED,
        .grp_size   = 0x04,
        .size_init  = xen_pt_reg_grp_size_init,
    },
    /* MSI Capability Structure reg group */
    {
        .grp_id      = PCI_CAP_ID_MSI,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0xFF,
        .size_init   = xen_pt_msi_size_init,
        .emu_regs = xen_pt_emu_reg_msi,
    },
    /* PCI-X Capabilities List Item reg group */
    {
        .grp_id     = PCI_CAP_ID_PCIX,
        .grp_type   = XEN_PT_GRP_TYPE_HARDWIRED,
        .grp_size   = 0x18,
        .size_init  = xen_pt_reg_grp_size_init,
    },
    /* Vendor Specific Capability Structure reg group */
    {
        .grp_id      = PCI_CAP_ID_VNDR,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0xFF,
        .size_init   = xen_pt_vendor_size_init,
        .emu_regs = xen_pt_emu_reg_vendor,
    },
    /* SHPC Capability List Item reg group */
    {
        .grp_id     = PCI_CAP_ID_SHPC,
        .grp_type   = XEN_PT_GRP_TYPE_HARDWIRED,
        .grp_size   = 0x08,
        .size_init  = xen_pt_reg_grp_size_init,
    },
    /* Subsystem ID and Subsystem Vendor ID Capability List Item reg group */
    {
        .grp_id     = PCI_CAP_ID_SSVID,
        .grp_type   = XEN_PT_GRP_TYPE_HARDWIRED,
        .grp_size   = 0x08,
        .size_init  = xen_pt_reg_grp_size_init,
    },
    /* AGP 8x Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_AGP3,
        .grp_type   = XEN_PT_GRP_TYPE_HARDWIRED,
        .grp_size   = 0x30,
        .size_init  = xen_pt_reg_grp_size_init,
    },
    /* PCI Express Capability Structure reg group */
    {
        .grp_id      = PCI_CAP_ID_EXP,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0xFF,
        .size_init   = xen_pt_pcie_size_init,
        .emu_regs = xen_pt_emu_reg_pcie,
    },
    /* MSI-X Capability Structure reg group */
    {
        .grp_id      = PCI_CAP_ID_MSIX,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0x0C,
        .size_init   = xen_pt_msix_size_init,
        .emu_regs = xen_pt_emu_reg_msix,
    },
    /* Intel IGD Opregion group */
    {
        .grp_id      = XEN_PCI_INTEL_OPREGION,
        .grp_type    = XEN_PT_GRP_TYPE_EMU,
        .grp_size    = 0x4,
        .size_init   = xen_pt_reg_grp_size_init,
        .emu_regs    = xen_pt_emu_reg_igd_opregion,
    },
    {
        .grp_size = 0,
    },
};

/* initialize Capabilities Pointer or Next Pointer register */
static int xen_pt_ptr_reg_init(XenPCIPassthroughState *s,
                               XenPTRegInfo *reg, uint32_t real_offset,
                               uint32_t *data)
{
    int i, rc;
    uint8_t reg_field;
    uint8_t cap_id = 0;

    rc = xen_host_pci_get_byte(&s->real_device, real_offset, &reg_field);
    if (rc) {
        return rc;
    }
    /* find capability offset */
    while (reg_field) {
        for (i = 0; xen_pt_emu_reg_grps[i].grp_size != 0; i++) {
            if (xen_pt_hide_dev_cap(&s->real_device,
                                    xen_pt_emu_reg_grps[i].grp_id)) {
                continue;
            }

            rc = xen_host_pci_get_byte(&s->real_device,
                                       reg_field + PCI_CAP_LIST_ID, &cap_id);
            if (rc) {
                XEN_PT_ERR(&s->dev, "Failed to read capability @0x%x (rc:%d)\n",
                           reg_field + PCI_CAP_LIST_ID, rc);
                return rc;
            }
            if (xen_pt_emu_reg_grps[i].grp_id == cap_id) {
                if (xen_pt_emu_reg_grps[i].grp_type == XEN_PT_GRP_TYPE_EMU) {
                    goto out;
                }
                /* ignore the 0 hardwired capability, find next one */
                break;
            }
        }

        /* next capability */
        rc = xen_host_pci_get_byte(&s->real_device,
                                   reg_field + PCI_CAP_LIST_NEXT, &reg_field);
        if (rc) {
            return rc;
        }
    }

out:
    *data = reg_field;
    return 0;
}


/*************
 * Main
 */

static uint8_t find_cap_offset(XenPCIPassthroughState *s, uint8_t cap)
{
    uint8_t id;
    unsigned max_cap = XEN_PCI_CAP_MAX;
    uint8_t pos = PCI_CAPABILITY_LIST;
    uint8_t status = 0;

    if (xen_host_pci_get_byte(&s->real_device, PCI_STATUS, &status)) {
        return 0;
    }
    if ((status & PCI_STATUS_CAP_LIST) == 0) {
        return 0;
    }

    while (max_cap--) {
        if (xen_host_pci_get_byte(&s->real_device, pos, &pos)) {
            break;
        }
        if (pos < PCI_CONFIG_HEADER_SIZE) {
            break;
        }

        pos &= ~3;
        if (xen_host_pci_get_byte(&s->real_device,
                                  pos + PCI_CAP_LIST_ID, &id)) {
            break;
        }

        if (id == 0xff) {
            break;
        }
        if (id == cap) {
            return pos;
        }

        pos += PCI_CAP_LIST_NEXT;
    }
    return 0;
}

static void xen_pt_config_reg_init(XenPCIPassthroughState *s,
                                   XenPTRegGroup *reg_grp, XenPTRegInfo *reg,
                                   Error **errp)
{
    XenPTReg *reg_entry;
    uint32_t data = 0;
    int rc = 0;

    reg_entry = g_new0(XenPTReg, 1);
    reg_entry->reg = reg;

    if (reg->init) {
        uint32_t host_mask, size_mask;
        unsigned int offset;
        uint32_t val;

        /* initialize emulate register */
        rc = reg->init(s, reg_entry->reg,
                       reg_grp->base_offset + reg->offset, &data);
        if (rc < 0) {
            g_free(reg_entry);
            error_setg(errp, "Init emulate register fail");
            return;
        }
        if (data == XEN_PT_INVALID_REG) {
            /* free unused BAR register entry */
            g_free(reg_entry);
            return;
        }
        /* Sync up the data to dev.config */
        offset = reg_grp->base_offset + reg->offset;
        size_mask = 0xFFFFFFFF >> ((4 - reg->size) << 3);

        switch (reg->size) {
        case 1: rc = xen_host_pci_get_byte(&s->real_device, offset, (uint8_t *)&val);
                break;
        case 2: rc = xen_host_pci_get_word(&s->real_device, offset, (uint16_t *)&val);
                break;
        case 4: rc = xen_host_pci_get_long(&s->real_device, offset, &val);
                break;
        default: abort();
        }
        if (rc) {
            /* Serious issues when we cannot read the host values! */
            g_free(reg_entry);
            error_setg(errp, "Cannot read host values");
            return;
        }
        /* Set bits in emu_mask are the ones we emulate. The dev.config shall
         * contain the emulated view of the guest - therefore we flip the mask
         * to mask out the host values (which dev.config initially has) . */
        host_mask = size_mask & ~reg->emu_mask;

        if ((data & host_mask) != (val & host_mask)) {
            uint32_t new_val;

            /* Mask out host (including past size). */
            new_val = val & host_mask;
            /* Merge emulated ones (excluding the non-emulated ones). */
            new_val |= data & host_mask;
            /* Leave intact host and emulated values past the size - even though
             * we do not care as we write per reg->size granularity, but for the
             * logging below lets have the proper value. */
            new_val |= ((val | data)) & ~size_mask;
            XEN_PT_LOG(&s->dev,"Offset 0x%04x mismatch! Emulated=0x%04x, host=0x%04x, syncing to 0x%04x.\n",
                       offset, data, val, new_val);
            val = new_val;
        } else
            val = data;

        if (val & ~size_mask) {
            error_setg(errp, "Offset 0x%04x:0x%04x expands past"
                    " register size (%d)", offset, val, reg->size);
            g_free(reg_entry);
            return;
        }
        /* This could be just pci_set_long as we don't modify the bits
         * past reg->size, but in case this routine is run in parallel or the
         * init value is larger, we do not want to over-write registers. */
        switch (reg->size) {
        case 1: pci_set_byte(s->dev.config + offset, (uint8_t)val);
                break;
        case 2: pci_set_word(s->dev.config + offset, (uint16_t)val);
                break;
        case 4: pci_set_long(s->dev.config + offset, val);
                break;
        default: abort();
        }
        /* set register value pointer to the data. */
        reg_entry->ptr.byte = s->dev.config + offset;

    }
    /* list add register entry */
    QLIST_INSERT_HEAD(&reg_grp->reg_tbl_list, reg_entry, entries);
}

void xen_pt_config_init(XenPCIPassthroughState *s, Error **errp)
{
    int i, rc;
    Error *err = NULL;

    QLIST_INIT(&s->reg_grps);

    for (i = 0; xen_pt_emu_reg_grps[i].grp_size != 0; i++) {
        uint32_t reg_grp_offset = 0;
        XenPTRegGroup *reg_grp_entry = NULL;

        if (xen_pt_emu_reg_grps[i].grp_id != 0xFF
            && xen_pt_emu_reg_grps[i].grp_id != XEN_PCI_INTEL_OPREGION) {
            if (xen_pt_hide_dev_cap(&s->real_device,
                                    xen_pt_emu_reg_grps[i].grp_id)) {
                continue;
            }

            reg_grp_offset = find_cap_offset(s, xen_pt_emu_reg_grps[i].grp_id);

            if (!reg_grp_offset) {
                continue;
            }
        }

        /*
         * By default we will trap up to 0x40 in the cfg space.
         * If an intel device is pass through we need to trap 0xfc,
         * therefore the size should be 0xff.
         */
        if (xen_pt_emu_reg_grps[i].grp_id == XEN_PCI_INTEL_OPREGION) {
            reg_grp_offset = XEN_PCI_INTEL_OPREGION;
        }

        reg_grp_entry = g_new0(XenPTRegGroup, 1);
        QLIST_INIT(&reg_grp_entry->reg_tbl_list);
        QLIST_INSERT_HEAD(&s->reg_grps, reg_grp_entry, entries);

        reg_grp_entry->base_offset = reg_grp_offset;
        reg_grp_entry->reg_grp = xen_pt_emu_reg_grps + i;
        if (xen_pt_emu_reg_grps[i].size_init) {
            /* get register group size */
            rc = xen_pt_emu_reg_grps[i].size_init(s, reg_grp_entry->reg_grp,
                                                  reg_grp_offset,
                                                  &reg_grp_entry->size);
            if (rc < 0) {
                error_setg(&err, "Failed to initialize %d/%zu, type = 0x%x,"
                           " rc: %d", i, ARRAY_SIZE(xen_pt_emu_reg_grps),
                           xen_pt_emu_reg_grps[i].grp_type, rc);
                error_propagate(errp, err);
                xen_pt_config_delete(s);
                return;
            }
        }

        if (xen_pt_emu_reg_grps[i].grp_type == XEN_PT_GRP_TYPE_EMU) {
            if (xen_pt_emu_reg_grps[i].emu_regs) {
                int j = 0;
                XenPTRegInfo *regs = xen_pt_emu_reg_grps[i].emu_regs;

                /* initialize capability register */
                for (j = 0; regs->size != 0; j++, regs++) {
                    xen_pt_config_reg_init(s, reg_grp_entry, regs, &err);
                    if (err) {
                        error_append_hint(&err, "Failed to init register %d"
                                " offsets 0x%x in grp_type = 0x%x (%d/%zu)", j,
                                regs->offset, xen_pt_emu_reg_grps[i].grp_type,
                                i, ARRAY_SIZE(xen_pt_emu_reg_grps));
                        error_propagate(errp, err);
                        xen_pt_config_delete(s);
                        return;
                    }
                }
            }
        }
    }
}

/* delete all emulate register */
void xen_pt_config_delete(XenPCIPassthroughState *s)
{
    struct XenPTRegGroup *reg_group, *next_grp;
    struct XenPTReg *reg, *next_reg;

    /* free MSI/MSI-X info table */
    if (s->msix) {
        xen_pt_msix_unmap(s);
    }
    g_free(s->msi);

    /* free all register group entry */
    QLIST_FOREACH_SAFE(reg_group, &s->reg_grps, entries, next_grp) {
        /* free all register entry */
        QLIST_FOREACH_SAFE(reg, &reg_group->reg_tbl_list, entries, next_reg) {
            QLIST_REMOVE(reg, entries);
            g_free(reg);
        }

        QLIST_REMOVE(reg_group, entries);
        g_free(reg_group);
    }
}
