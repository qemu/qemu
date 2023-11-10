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

/*
 * Interrupt Disable policy:
 *
 * INTx interrupt:
 *   Initialize(register_real_device)
 *     Map INTx(xc_physdev_map_pirq):
 *       <fail>
 *         - Set real Interrupt Disable bit to '1'.
 *         - Set machine_irq and assigned_device->machine_irq to '0'.
 *         * Don't bind INTx.
 *
 *     Bind INTx(xc_domain_bind_pt_pci_irq):
 *       <fail>
 *         - Set real Interrupt Disable bit to '1'.
 *         - Unmap INTx.
 *         - Decrement xen_pt_mapped_machine_irq[machine_irq]
 *         - Set assigned_device->machine_irq to '0'.
 *
 *   Write to Interrupt Disable bit by guest software(xen_pt_cmd_reg_write)
 *     Write '0'
 *       - Set real bit to '0' if assigned_device->machine_irq isn't '0'.
 *
 *     Write '1'
 *       - Set real bit to '1'.
 *
 * MSI interrupt:
 *   Initialize MSI register(xen_pt_msi_setup, xen_pt_msi_update)
 *     Bind MSI(xc_domain_update_msi_irq)
 *       <fail>
 *         - Unmap MSI.
 *         - Set dev->msi->pirq to '-1'.
 *
 * MSI-X interrupt:
 *   Initialize MSI-X register(xen_pt_msix_update_one)
 *     Bind MSI-X(xc_domain_update_msi_irq)
 *       <fail>
 *         - Unmap MSI-X.
 *         - Set entry->pirq to '-1'.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include <sys/ioctl.h>

#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/xen/xen_pt.h"
#include "hw/xen/xen_igd.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen-legacy-backend.h"
#include "qemu/range.h"

static bool has_igd_gfx_passthru;

bool xen_igd_gfx_pt_enabled(void)
{
    return has_igd_gfx_passthru;
}

void xen_igd_gfx_pt_set(bool value, Error **errp)
{
    has_igd_gfx_passthru = value;
}

#define XEN_PT_NR_IRQS (256)
static uint8_t xen_pt_mapped_machine_irq[XEN_PT_NR_IRQS] = {0};

void xen_pt_log(const PCIDevice *d, const char *f, ...)
{
    va_list ap;

    va_start(ap, f);
    if (d) {
        fprintf(stderr, "[%02x:%02x.%d] ", pci_dev_bus_num(d),
                PCI_SLOT(d->devfn), PCI_FUNC(d->devfn));
    }
    vfprintf(stderr, f, ap);
    va_end(ap);
}

/* Config Space */

static int xen_pt_pci_config_access_check(PCIDevice *d, uint32_t addr, int len)
{
    /* check offset range */
    if (addr > 0xFF) {
        XEN_PT_ERR(d, "Failed to access register with offset exceeding 0xFF. "
                   "(addr: 0x%02x, len: %d)\n", addr, len);
        return -1;
    }

    /* check read size */
    if ((len != 1) && (len != 2) && (len != 4)) {
        XEN_PT_ERR(d, "Failed to access register with invalid access length. "
                   "(addr: 0x%02x, len: %d)\n", addr, len);
        return -1;
    }

    /* check offset alignment */
    if (addr & (len - 1)) {
        XEN_PT_ERR(d, "Failed to access register with invalid access size "
                   "alignment. (addr: 0x%02x, len: %d)\n", addr, len);
        return -1;
    }

    return 0;
}

int xen_pt_bar_offset_to_index(uint32_t offset)
{
    int index = 0;

    /* check Exp ROM BAR */
    if (offset == PCI_ROM_ADDRESS) {
        return PCI_ROM_SLOT;
    }

    /* calculate BAR index */
    index = (offset - PCI_BASE_ADDRESS_0) >> 2;
    if (index >= PCI_NUM_REGIONS) {
        return -1;
    }

    return index;
}

static uint32_t xen_pt_pci_read_config(PCIDevice *d, uint32_t addr, int len)
{
    XenPCIPassthroughState *s = XEN_PT_DEVICE(d);
    uint32_t val = 0;
    XenPTRegGroup *reg_grp_entry = NULL;
    XenPTReg *reg_entry = NULL;
    int rc = 0;
    int emul_len = 0;
    uint32_t find_addr = addr;

    if (xen_pt_pci_config_access_check(d, addr, len)) {
        goto exit;
    }

    /* find register group entry */
    reg_grp_entry = xen_pt_find_reg_grp(s, addr);
    if (reg_grp_entry) {
        /* check 0-Hardwired register group */
        if (reg_grp_entry->reg_grp->grp_type == XEN_PT_GRP_TYPE_HARDWIRED) {
            /* no need to emulate, just return 0 */
            val = 0;
            goto exit;
        }
    }

    /* read I/O device register value */
    rc = xen_host_pci_get_block(&s->real_device, addr, (uint8_t *)&val, len);
    if (rc < 0) {
        XEN_PT_ERR(d, "pci_read_block failed. return value: %d.\n", rc);
        memset(&val, 0xff, len);
    }

    /* just return the I/O device register value for
     * passthrough type register group */
    if (reg_grp_entry == NULL) {
        goto exit;
    }

    /* adjust the read value to appropriate CFC-CFF window */
    val <<= (addr & 3) << 3;
    emul_len = len;

    /* loop around the guest requested size */
    while (emul_len > 0) {
        /* find register entry to be emulated */
        reg_entry = xen_pt_find_reg(reg_grp_entry, find_addr);
        if (reg_entry) {
            XenPTRegInfo *reg = reg_entry->reg;
            uint32_t real_offset = reg_grp_entry->base_offset + reg->offset;
            uint32_t valid_mask = 0xFFFFFFFF >> ((4 - emul_len) << 3);
            uint8_t *ptr_val = NULL;

            valid_mask <<= (find_addr - real_offset) << 3;
            ptr_val = (uint8_t *)&val + (real_offset & 3);

            /* do emulation based on register size */
            switch (reg->size) {
            case 1:
                if (reg->u.b.read) {
                    rc = reg->u.b.read(s, reg_entry, ptr_val, valid_mask);
                }
                break;
            case 2:
                if (reg->u.w.read) {
                    rc = reg->u.w.read(s, reg_entry,
                                       (uint16_t *)ptr_val, valid_mask);
                }
                break;
            case 4:
                if (reg->u.dw.read) {
                    rc = reg->u.dw.read(s, reg_entry,
                                        (uint32_t *)ptr_val, valid_mask);
                }
                break;
            }

            if (rc < 0) {
                xen_shutdown_fatal_error("Internal error: Invalid read "
                                         "emulation. (%s, rc: %d)\n",
                                         __func__, rc);
                return 0;
            }

            /* calculate next address to find */
            emul_len -= reg->size;
            if (emul_len > 0) {
                find_addr = real_offset + reg->size;
            }
        } else {
            /* nothing to do with passthrough type register,
             * continue to find next byte */
            emul_len--;
            find_addr++;
        }
    }

    /* need to shift back before returning them to pci bus emulator */
    val >>= ((addr & 3) << 3);

exit:
    XEN_PT_LOG_CONFIG(d, addr, val, len);
    return val;
}

static void xen_pt_pci_write_config(PCIDevice *d, uint32_t addr,
                                    uint32_t val, int len)
{
    XenPCIPassthroughState *s = XEN_PT_DEVICE(d);
    int index = 0;
    XenPTRegGroup *reg_grp_entry = NULL;
    int rc = 0;
    uint32_t read_val = 0, wb_mask;
    int emul_len = 0;
    XenPTReg *reg_entry = NULL;
    uint32_t find_addr = addr;
    XenPTRegInfo *reg = NULL;
    bool wp_flag = false;

    if (xen_pt_pci_config_access_check(d, addr, len)) {
        return;
    }

    XEN_PT_LOG_CONFIG(d, addr, val, len);

    /* check unused BAR register */
    index = xen_pt_bar_offset_to_index(addr);
    if ((index >= 0) && (val != 0)) {
        uint32_t chk = val;

        if (index == PCI_ROM_SLOT)
            chk |= (uint32_t)~PCI_ROM_ADDRESS_MASK;

        if ((chk != XEN_PT_BAR_ALLF) &&
            (s->bases[index].bar_flag == XEN_PT_BAR_FLAG_UNUSED)) {
            XEN_PT_WARN(d, "Guest attempt to set address to unused "
                        "Base Address Register. (addr: 0x%02x, len: %d)\n",
                        addr, len);
        }
    }

    /* find register group entry */
    reg_grp_entry = xen_pt_find_reg_grp(s, addr);
    if (reg_grp_entry) {
        /* check 0-Hardwired register group */
        if (reg_grp_entry->reg_grp->grp_type == XEN_PT_GRP_TYPE_HARDWIRED) {
            /* ignore silently */
            XEN_PT_WARN(d, "Access to 0-Hardwired register. "
                        "(addr: 0x%02x, len: %d)\n", addr, len);
            return;
        }
    }

    rc = xen_host_pci_get_block(&s->real_device, addr,
                                (uint8_t *)&read_val, len);
    if (rc < 0) {
        XEN_PT_ERR(d, "pci_read_block failed. return value: %d.\n", rc);
        memset(&read_val, 0xff, len);
        wb_mask = 0;
    } else {
        wb_mask = 0xFFFFFFFF >> ((4 - len) << 3);
    }

    /* pass directly to the real device for passthrough type register group */
    if (reg_grp_entry == NULL) {
        if (!s->permissive) {
            wb_mask = 0;
            wp_flag = true;
        }
        goto out;
    }

    memory_region_transaction_begin();
    pci_default_write_config(d, addr, val, len);

    /* adjust the read and write value to appropriate CFC-CFF window */
    read_val <<= (addr & 3) << 3;
    val <<= (addr & 3) << 3;
    emul_len = len;

    /* loop around the guest requested size */
    while (emul_len > 0) {
        /* find register entry to be emulated */
        reg_entry = xen_pt_find_reg(reg_grp_entry, find_addr);
        if (reg_entry) {
            reg = reg_entry->reg;
            uint32_t real_offset = reg_grp_entry->base_offset + reg->offset;
            uint32_t valid_mask = 0xFFFFFFFF >> ((4 - emul_len) << 3);
            uint8_t *ptr_val = NULL;
            uint32_t wp_mask = reg->emu_mask | reg->ro_mask;

            valid_mask <<= (find_addr - real_offset) << 3;
            ptr_val = (uint8_t *)&val + (real_offset & 3);
            if (!s->permissive) {
                wp_mask |= reg->res_mask;
            }
            if (wp_mask == (0xFFFFFFFF >> ((4 - reg->size) << 3))) {
                wb_mask &= ~((wp_mask >> ((find_addr - real_offset) << 3))
                             << ((len - emul_len) << 3));
            }

            /* do emulation based on register size */
            switch (reg->size) {
            case 1:
                if (reg->u.b.write) {
                    rc = reg->u.b.write(s, reg_entry, ptr_val,
                                        read_val >> ((real_offset & 3) << 3),
                                        valid_mask);
                }
                break;
            case 2:
                if (reg->u.w.write) {
                    rc = reg->u.w.write(s, reg_entry, (uint16_t *)ptr_val,
                                        (read_val >> ((real_offset & 3) << 3)),
                                        valid_mask);
                }
                break;
            case 4:
                if (reg->u.dw.write) {
                    rc = reg->u.dw.write(s, reg_entry, (uint32_t *)ptr_val,
                                         (read_val >> ((real_offset & 3) << 3)),
                                         valid_mask);
                }
                break;
            }

            if (rc < 0) {
                xen_shutdown_fatal_error("Internal error: Invalid write"
                                         " emulation. (%s, rc: %d)\n",
                                         __func__, rc);
                return;
            }

            /* calculate next address to find */
            emul_len -= reg->size;
            if (emul_len > 0) {
                find_addr = real_offset + reg->size;
            }
        } else {
            /* nothing to do with passthrough type register,
             * continue to find next byte */
            if (!s->permissive) {
                wb_mask &= ~(0xff << ((len - emul_len) << 3));
                /* Unused BARs will make it here, but we don't want to issue
                 * warnings for writes to them (bogus writes get dealt with
                 * above).
                 */
                if (index < 0) {
                    wp_flag = true;
                }
            }
            emul_len--;
            find_addr++;
        }
    }

    /* need to shift back before passing them to xen_host_pci_set_block. */
    val >>= (addr & 3) << 3;

    memory_region_transaction_commit();

out:
    if (wp_flag && !s->permissive_warned) {
        s->permissive_warned = true;
        xen_pt_log(d, "Write-back to unknown field 0x%02x (partially) inhibited (0x%0*x)\n",
                   addr, len * 2, wb_mask);
        xen_pt_log(d, "If the device doesn't work, try enabling permissive mode\n");
        xen_pt_log(d, "(unsafe) and if it helps report the problem to xen-devel\n");
    }
    for (index = 0; wb_mask; index += len) {
        /* unknown regs are passed through */
        while (!(wb_mask & 0xff)) {
            index++;
            wb_mask >>= 8;
        }
        len = 0;
        do {
            len++;
            wb_mask >>= 8;
        } while (wb_mask & 0xff);
        rc = xen_host_pci_set_block(&s->real_device, addr + index,
                                    (uint8_t *)&val + index, len);

        if (rc < 0) {
            XEN_PT_ERR(d, "xen_host_pci_set_block failed. return value: %d.\n", rc);
        }
    }
}

/* register regions */

static uint64_t xen_pt_bar_read(void *o, hwaddr addr,
                                unsigned size)
{
    PCIDevice *d = o;
    /* if this function is called, that probably means that there is a
     * misconfiguration of the IOMMU. */
    XEN_PT_ERR(d, "Should not read BAR through QEMU. @0x"HWADDR_FMT_plx"\n",
               addr);
    return 0;
}
static void xen_pt_bar_write(void *o, hwaddr addr, uint64_t val,
                             unsigned size)
{
    PCIDevice *d = o;
    /* Same comment as xen_pt_bar_read function */
    XEN_PT_ERR(d, "Should not write BAR through QEMU. @0x"HWADDR_FMT_plx"\n",
               addr);
}

static const MemoryRegionOps ops = {
    .endianness = DEVICE_NATIVE_ENDIAN,
    .read = xen_pt_bar_read,
    .write = xen_pt_bar_write,
};

static int xen_pt_register_regions(XenPCIPassthroughState *s, uint16_t *cmd)
{
    int i = 0;
    XenHostPCIDevice *d = &s->real_device;

    /* Register PIO/MMIO BARs */
    for (i = 0; i < PCI_ROM_SLOT; i++) {
        XenHostPCIIORegion *r = &d->io_regions[i];
        uint8_t type;

        if (r->base_addr == 0 || r->size == 0) {
            continue;
        }

        s->bases[i].access.u = r->base_addr;

        if (r->type & XEN_HOST_PCI_REGION_TYPE_IO) {
            type = PCI_BASE_ADDRESS_SPACE_IO;
            *cmd |= PCI_COMMAND_IO;
        } else {
            type = PCI_BASE_ADDRESS_SPACE_MEMORY;
            if (r->type & XEN_HOST_PCI_REGION_TYPE_PREFETCH) {
                type |= PCI_BASE_ADDRESS_MEM_PREFETCH;
            }
            if (r->type & XEN_HOST_PCI_REGION_TYPE_MEM_64) {
                type |= PCI_BASE_ADDRESS_MEM_TYPE_64;
            }
            *cmd |= PCI_COMMAND_MEMORY;
        }

        memory_region_init_io(&s->bar[i], OBJECT(s), &ops, &s->dev,
                              "xen-pci-pt-bar", r->size);
        pci_register_bar(&s->dev, i, type, &s->bar[i]);

        XEN_PT_LOG(&s->dev, "IO region %i registered (size=0x%08"PRIx64
                   " base_addr=0x%08"PRIx64" type: 0x%x)\n",
                   i, r->size, r->base_addr, type);
    }

    /* Register expansion ROM address */
    if (d->rom.base_addr && d->rom.size) {
        uint32_t bar_data = 0;

        /* Re-set BAR reported by OS, otherwise ROM can't be read. */
        if (xen_host_pci_get_long(d, PCI_ROM_ADDRESS, &bar_data)) {
            return 0;
        }
        if ((bar_data & PCI_ROM_ADDRESS_MASK) == 0) {
            bar_data |= d->rom.base_addr & PCI_ROM_ADDRESS_MASK;
            xen_host_pci_set_long(d, PCI_ROM_ADDRESS, bar_data);
        }

        s->bases[PCI_ROM_SLOT].access.maddr = d->rom.base_addr;

        memory_region_init_io(&s->rom, OBJECT(s), &ops, &s->dev,
                              "xen-pci-pt-rom", d->rom.size);
        pci_register_bar(&s->dev, PCI_ROM_SLOT, PCI_BASE_ADDRESS_MEM_PREFETCH,
                         &s->rom);

        XEN_PT_LOG(&s->dev, "Expansion ROM registered (size=0x%08"PRIx64
                   " base_addr=0x%08"PRIx64")\n",
                   d->rom.size, d->rom.base_addr);
    }

    xen_pt_register_vga_regions(d);
    return 0;
}

/* region mapping */

static int xen_pt_bar_from_region(XenPCIPassthroughState *s, MemoryRegion *mr)
{
    int i = 0;

    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        if (mr == &s->bar[i]) {
            return i;
        }
    }
    if (mr == &s->rom) {
        return PCI_ROM_SLOT;
    }
    return -1;
}

/*
 * This function checks if an io_region overlaps an io_region from another
 * device.  The io_region to check is provided with (addr, size and type)
 * A callback can be provided and will be called for every region that is
 * overlapped.
 * The return value indicates if the region is overlappsed */
struct CheckBarArgs {
    XenPCIPassthroughState *s;
    pcibus_t addr;
    pcibus_t size;
    uint8_t type;
    bool rc;
};
static void xen_pt_check_bar_overlap(PCIBus *bus, PCIDevice *d, void *opaque)
{
    struct CheckBarArgs *arg = opaque;
    XenPCIPassthroughState *s = arg->s;
    uint8_t type = arg->type;
    int i;

    if (d->devfn == s->dev.devfn) {
        return;
    }

    /* xxx: This ignores bridges. */
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        const PCIIORegion *r = &d->io_regions[i];

        if (!r->size) {
            continue;
        }
        if ((type & PCI_BASE_ADDRESS_SPACE_IO)
            != (r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            continue;
        }

        if (ranges_overlap(arg->addr, arg->size, r->addr, r->size)) {
            XEN_PT_WARN(&s->dev,
                        "Overlapped to device [%02x:%02x.%d] Region: %i"
                        " (addr: 0x%"FMT_PCIBUS", len: 0x%"FMT_PCIBUS")\n",
                        pci_bus_num(bus), PCI_SLOT(d->devfn),
                        PCI_FUNC(d->devfn), i, r->addr, r->size);
            arg->rc = true;
        }
    }
}

static void xen_pt_region_update(XenPCIPassthroughState *s,
                                 MemoryRegionSection *sec, bool adding)
{
    PCIDevice *d = &s->dev;
    MemoryRegion *mr = sec->mr;
    int bar = -1;
    int rc;
    int op = adding ? DPCI_ADD_MAPPING : DPCI_REMOVE_MAPPING;
    struct CheckBarArgs args = {
        .s = s,
        .addr = sec->offset_within_address_space,
        .size = int128_get64(sec->size),
        .rc = false,
    };

    bar = xen_pt_bar_from_region(s, mr);
    if (bar == -1 && (!s->msix || &s->msix->mmio != mr)) {
        return;
    }

    if (s->msix && &s->msix->mmio == mr) {
        if (adding) {
            s->msix->mmio_base_addr = sec->offset_within_address_space;
            rc = xen_pt_msix_update_remap(s, s->msix->bar_index);
        }
        return;
    }

    args.type = d->io_regions[bar].type;
    pci_for_each_device_under_bus(pci_get_bus(d),
                                  xen_pt_check_bar_overlap, &args);
    if (args.rc) {
        XEN_PT_WARN(d, "Region: %d (addr: 0x%"FMT_PCIBUS
                    ", len: 0x%"FMT_PCIBUS") is overlapped.\n",
                    bar, sec->offset_within_address_space,
                    int128_get64(sec->size));
    }

    if (d->io_regions[bar].type & PCI_BASE_ADDRESS_SPACE_IO) {
        uint32_t guest_port = sec->offset_within_address_space;
        uint32_t machine_port = s->bases[bar].access.pio_base;
        uint32_t size = int128_get64(sec->size);
        rc = xc_domain_ioport_mapping(xen_xc, xen_domid,
                                      guest_port, machine_port, size,
                                      op);
        if (rc) {
            XEN_PT_ERR(d, "%s ioport mapping failed! (err: %i)\n",
                       adding ? "create new" : "remove old", errno);
        }
    } else {
        pcibus_t guest_addr = sec->offset_within_address_space;
        pcibus_t machine_addr = s->bases[bar].access.maddr
            + sec->offset_within_region;
        pcibus_t size = int128_get64(sec->size);
        rc = xc_domain_memory_mapping(xen_xc, xen_domid,
                                      XEN_PFN(guest_addr + XC_PAGE_SIZE - 1),
                                      XEN_PFN(machine_addr + XC_PAGE_SIZE - 1),
                                      XEN_PFN(size + XC_PAGE_SIZE - 1),
                                      op);
        if (rc) {
            XEN_PT_ERR(d, "%s mem mapping failed! (err: %i)\n",
                       adding ? "create new" : "remove old", errno);
        }
    }
}

static void xen_pt_region_add(MemoryListener *l, MemoryRegionSection *sec)
{
    XenPCIPassthroughState *s = container_of(l, XenPCIPassthroughState,
                                             memory_listener);

    memory_region_ref(sec->mr);
    xen_pt_region_update(s, sec, true);
}

static void xen_pt_region_del(MemoryListener *l, MemoryRegionSection *sec)
{
    XenPCIPassthroughState *s = container_of(l, XenPCIPassthroughState,
                                             memory_listener);

    xen_pt_region_update(s, sec, false);
    memory_region_unref(sec->mr);
}

static void xen_pt_io_region_add(MemoryListener *l, MemoryRegionSection *sec)
{
    XenPCIPassthroughState *s = container_of(l, XenPCIPassthroughState,
                                             io_listener);

    memory_region_ref(sec->mr);
    xen_pt_region_update(s, sec, true);
}

static void xen_pt_io_region_del(MemoryListener *l, MemoryRegionSection *sec)
{
    XenPCIPassthroughState *s = container_of(l, XenPCIPassthroughState,
                                             io_listener);

    xen_pt_region_update(s, sec, false);
    memory_region_unref(sec->mr);
}

static const MemoryListener xen_pt_memory_listener = {
    .name = "xen-pt-mem",
    .region_add = xen_pt_region_add,
    .region_del = xen_pt_region_del,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

static const MemoryListener xen_pt_io_listener = {
    .name = "xen-pt-io",
    .region_add = xen_pt_io_region_add,
    .region_del = xen_pt_io_region_del,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

/* destroy. */
static void xen_pt_destroy(PCIDevice *d) {

    XenPCIPassthroughState *s = XEN_PT_DEVICE(d);
    XenHostPCIDevice *host_dev = &s->real_device;
    uint8_t machine_irq = s->machine_irq;
    uint8_t intx;
    int rc;

    if (machine_irq && !xen_host_pci_device_closed(&s->real_device)) {
        intx = xen_pt_pci_intx(s);
        rc = xc_domain_unbind_pt_irq(xen_xc, xen_domid, machine_irq,
                                     PT_IRQ_TYPE_PCI,
                                     pci_dev_bus_num(d),
                                     PCI_SLOT(s->dev.devfn),
                                     intx,
                                     0 /* isa_irq */);
        if (rc < 0) {
            XEN_PT_ERR(d, "unbinding of interrupt INT%c failed."
                       " (machine irq: %i, err: %d)"
                       " But bravely continuing on..\n",
                       'a' + intx, machine_irq, errno);
        }
    }

    /* N.B. xen_pt_config_delete takes care of freeing them. */
    if (s->msi) {
        xen_pt_msi_disable(s);
    }
    if (s->msix) {
        xen_pt_msix_disable(s);
    }

    if (machine_irq) {
        xen_pt_mapped_machine_irq[machine_irq]--;

        if (xen_pt_mapped_machine_irq[machine_irq] == 0) {
            rc = xc_physdev_unmap_pirq(xen_xc, xen_domid, machine_irq);

            if (rc < 0) {
                XEN_PT_ERR(d, "unmapping of interrupt %i failed. (err: %d)"
                           " But bravely continuing on..\n",
                           machine_irq, errno);
            }
        }
        s->machine_irq = 0;
    }

    /* delete all emulated config registers */
    xen_pt_config_delete(s);

    xen_pt_unregister_vga_regions(host_dev);

    if (s->listener_set) {
        memory_listener_unregister(&s->memory_listener);
        memory_listener_unregister(&s->io_listener);
        s->listener_set = false;
    }
    if (!xen_host_pci_device_closed(&s->real_device)) {
        xen_host_pci_device_put(&s->real_device);
    }
}
/* init */

static void xen_pt_realize(PCIDevice *d, Error **errp)
{
    ERRP_GUARD();
    XenPCIPassthroughState *s = XEN_PT_DEVICE(d);
    int i, rc = 0;
    uint8_t machine_irq = 0, scratch;
    uint16_t cmd = 0;
    int pirq = XEN_PT_UNASSIGNED_PIRQ;

    /* register real device */
    XEN_PT_LOG(d, "Assigning real physical device %02x:%02x.%d"
               " to devfn 0x%x\n",
               s->hostaddr.bus, s->hostaddr.slot, s->hostaddr.function,
               s->dev.devfn);

    s->is_virtfn = s->real_device.is_virtfn;
    if (s->is_virtfn) {
        XEN_PT_LOG(d, "%04x:%02x:%02x.%d is a SR-IOV Virtual Function\n",
                   s->real_device.domain, s->real_device.bus,
                   s->real_device.dev, s->real_device.func);
    }

    /* Initialize virtualized PCI configuration (Extended 256 Bytes) */
    memset(d->config, 0, PCI_CONFIG_SPACE_SIZE);

    s->memory_listener = xen_pt_memory_listener;
    s->io_listener = xen_pt_io_listener;

    /* Setup VGA bios for passthrough GFX */
    if ((s->real_device.domain == XEN_PCI_IGD_DOMAIN) &&
        (s->real_device.bus == XEN_PCI_IGD_BUS) &&
        (s->real_device.dev == XEN_PCI_IGD_DEV) &&
        (s->real_device.func == XEN_PCI_IGD_FN)) {
        if (!is_igd_vga_passthrough(&s->real_device)) {
            error_setg(errp, "Need to enable igd-passthru if you're trying"
                    " to passthrough IGD GFX");
            xen_host_pci_device_put(&s->real_device);
            return;
        }

        xen_pt_setup_vga(s, &s->real_device, errp);
        if (*errp) {
            error_append_hint(errp, "Setup VGA BIOS of passthrough"
                              " GFX failed");
            xen_host_pci_device_put(&s->real_device);
            return;
        }

        /* Register ISA bridge for passthrough GFX. */
        xen_igd_passthrough_isa_bridge_create(s, &s->real_device);
    }

    /* Handle real device's MMIO/PIO BARs */
    xen_pt_register_regions(s, &cmd);

    /* reinitialize each config register to be emulated */
    xen_pt_config_init(s, errp);
    if (*errp) {
        error_append_hint(errp, "PCI Config space initialisation failed");
        rc = -1;
        goto err_out;
    }

    /* Bind interrupt */
    rc = xen_host_pci_get_byte(&s->real_device, PCI_INTERRUPT_PIN, &scratch);
    if (rc) {
        error_setg_errno(errp, errno, "Failed to read PCI_INTERRUPT_PIN");
        goto err_out;
    }
    if (!scratch) {
        XEN_PT_LOG(d, "no pin interrupt\n");
        goto out;
    }

    machine_irq = s->real_device.irq;
    if (machine_irq == 0) {
        XEN_PT_LOG(d, "machine irq is 0\n");
        cmd |= PCI_COMMAND_INTX_DISABLE;
        goto out;
    }

    rc = xc_physdev_map_pirq(xen_xc, xen_domid, machine_irq, &pirq);
    if (rc < 0) {
        XEN_PT_ERR(d, "Mapping machine irq %u to pirq %i failed, (err: %d)\n",
                   machine_irq, pirq, errno);

        /* Disable PCI intx assertion (turn on bit10 of devctl) */
        cmd |= PCI_COMMAND_INTX_DISABLE;
        machine_irq = 0;
        s->machine_irq = 0;
    } else {
        machine_irq = pirq;
        s->machine_irq = pirq;
        xen_pt_mapped_machine_irq[machine_irq]++;
    }

    /* bind machine_irq to device */
    if (machine_irq != 0) {
        uint8_t e_intx = xen_pt_pci_intx(s);

        rc = xc_domain_bind_pt_pci_irq(xen_xc, xen_domid, machine_irq,
                                       pci_dev_bus_num(d),
                                       PCI_SLOT(d->devfn),
                                       e_intx);
        if (rc < 0) {
            XEN_PT_ERR(d, "Binding of interrupt %i failed! (err: %d)\n",
                       e_intx, errno);

            /* Disable PCI intx assertion (turn on bit10 of devctl) */
            cmd |= PCI_COMMAND_INTX_DISABLE;
            xen_pt_mapped_machine_irq[machine_irq]--;

            if (xen_pt_mapped_machine_irq[machine_irq] == 0) {
                if (xc_physdev_unmap_pirq(xen_xc, xen_domid, machine_irq)) {
                    XEN_PT_ERR(d, "Unmapping of machine interrupt %i failed!"
                               " (err: %d)\n", machine_irq, errno);
                }
            }
            s->machine_irq = 0;
        }
    }

out:
    if (cmd) {
        uint16_t val;

        rc = xen_host_pci_get_word(&s->real_device, PCI_COMMAND, &val);
        if (rc) {
            error_setg_errno(errp, errno, "Failed to read PCI_COMMAND");
            goto err_out;
        } else {
            val |= cmd;
            rc = xen_host_pci_set_word(&s->real_device, PCI_COMMAND, val);
            if (rc) {
                error_setg_errno(errp, errno, "Failed to write PCI_COMMAND"
                                 " val = 0x%x", val);
                goto err_out;
            }
        }
    }

    memory_listener_register(&s->memory_listener, &address_space_memory);
    memory_listener_register(&s->io_listener, &address_space_io);
    s->listener_set = true;
    XEN_PT_LOG(d,
               "Real physical device %02x:%02x.%d registered successfully\n",
               s->hostaddr.bus, s->hostaddr.slot, s->hostaddr.function);

    return;

err_out:
    for (i = 0; i < PCI_ROM_SLOT; i++) {
        object_unparent(OBJECT(&s->bar[i]));
    }
    object_unparent(OBJECT(&s->rom));

    xen_pt_destroy(d);
    assert(rc);
}

static void xen_pt_unregister_device(PCIDevice *d)
{
    xen_pt_destroy(d);
}

static Property xen_pci_passthrough_properties[] = {
    DEFINE_PROP_PCI_HOST_DEVADDR("hostaddr", XenPCIPassthroughState, hostaddr),
    DEFINE_PROP_BOOL("permissive", XenPCIPassthroughState, permissive, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void xen_pci_passthrough_instance_init(Object *obj)
{
    /* QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices */
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

void xen_igd_reserve_slot(PCIBus *pci_bus)
{
    if (!xen_igd_gfx_pt_enabled()) {
        return;
    }

    XEN_PT_LOG(0, "Reserving PCI slot 2 for IGD\n");
    pci_bus_set_slot_reserved_mask(pci_bus, XEN_PCI_IGD_SLOT_MASK);
}

static void xen_igd_clear_slot(DeviceState *qdev, Error **errp)
{
    ERRP_GUARD();
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    XenPCIPassthroughState *s = XEN_PT_DEVICE(pci_dev);
    XenPTDeviceClass *xpdc = XEN_PT_DEVICE_GET_CLASS(s);
    PCIBus *pci_bus = pci_get_bus(pci_dev);

    xen_host_pci_device_get(&s->real_device,
                            s->hostaddr.domain, s->hostaddr.bus,
                            s->hostaddr.slot, s->hostaddr.function,
                            errp);
    if (*errp) {
        error_append_hint(errp, "Failed to \"open\" the real pci device");
        return;
    }

    if (!(pci_bus_get_slot_reserved_mask(pci_bus) & XEN_PCI_IGD_SLOT_MASK)) {
        xpdc->pci_qdev_realize(qdev, errp);
        return;
    }

    if (is_igd_vga_passthrough(&s->real_device) &&
        s->real_device.domain == XEN_PCI_IGD_DOMAIN &&
        s->real_device.bus == XEN_PCI_IGD_BUS &&
        s->real_device.dev == XEN_PCI_IGD_DEV &&
        s->real_device.func == XEN_PCI_IGD_FN &&
        s->real_device.vendor_id == PCI_VENDOR_ID_INTEL) {
        pci_bus_clear_slot_reserved_mask(pci_bus, XEN_PCI_IGD_SLOT_MASK);
        XEN_PT_LOG(pci_dev, "Intel IGD found, using slot 2\n");
    }
    xpdc->pci_qdev_realize(qdev, errp);
}

static void xen_pci_passthrough_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    XenPTDeviceClass *xpdc = XEN_PT_DEVICE_CLASS(klass);
    xpdc->pci_qdev_realize = dc->realize;
    dc->realize = xen_igd_clear_slot;
    k->realize = xen_pt_realize;
    k->exit = xen_pt_unregister_device;
    k->config_read = xen_pt_pci_read_config;
    k->config_write = xen_pt_pci_write_config;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Assign an host PCI device with Xen";
    device_class_set_props(dc, xen_pci_passthrough_properties);
};

static void xen_pci_passthrough_finalize(Object *obj)
{
    XenPCIPassthroughState *s = XEN_PT_DEVICE(obj);

    xen_pt_msix_delete(s);
}

static const TypeInfo xen_pci_passthrough_info = {
    .name = TYPE_XEN_PT_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XenPCIPassthroughState),
    .instance_finalize = xen_pci_passthrough_finalize,
    .class_init = xen_pci_passthrough_class_init,
    .class_size = sizeof(XenPTDeviceClass),
    .instance_init = xen_pci_passthrough_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void xen_pci_passthrough_register_types(void)
{
    type_register_static(&xen_pci_passthrough_info);
}

type_init(xen_pci_passthrough_register_types)
