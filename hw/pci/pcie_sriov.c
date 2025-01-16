/*
 * pcie_sriov.c:
 *
 * Implementation of SR/IOV emulation support.
 *
 * Copyright (c) 2015-2017 Knut Omang <knut.omang@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "trace.h"

static void unparent_vfs(PCIDevice *dev, uint16_t total_vfs)
{
    for (uint16_t i = 0; i < total_vfs; i++) {
        PCIDevice *vf = dev->exp.sriov_pf.vf[i];
        object_unparent(OBJECT(vf));
        object_unref(OBJECT(vf));
    }
    g_free(dev->exp.sriov_pf.vf);
    dev->exp.sriov_pf.vf = NULL;
}

bool pcie_sriov_pf_init(PCIDevice *dev, uint16_t offset,
                        const char *vfname, uint16_t vf_dev_id,
                        uint16_t init_vfs, uint16_t total_vfs,
                        uint16_t vf_offset, uint16_t vf_stride,
                        Error **errp)
{
    BusState *bus = qdev_get_parent_bus(&dev->qdev);
    int32_t devfn = dev->devfn + vf_offset;
    uint8_t *cfg = dev->config + offset;
    uint8_t *wmask;

    if (total_vfs &&
        (uint32_t)devfn + (uint32_t)(total_vfs - 1) * vf_stride >= PCI_DEVFN_MAX) {
        error_setg(errp, "VF addr overflows");
        return false;
    }

    pcie_add_capability(dev, PCI_EXT_CAP_ID_SRIOV, 1,
                        offset, PCI_EXT_CAP_SRIOV_SIZEOF);
    dev->exp.sriov_cap = offset;
    dev->exp.sriov_pf.vf = NULL;

    pci_set_word(cfg + PCI_SRIOV_VF_OFFSET, vf_offset);
    pci_set_word(cfg + PCI_SRIOV_VF_STRIDE, vf_stride);

    /*
     * Mandatory page sizes to support.
     * Device implementations can call pcie_sriov_pf_add_sup_pgsize()
     * to set more bits:
     */
    pci_set_word(cfg + PCI_SRIOV_SUP_PGSIZE, SRIOV_SUP_PGSIZE_MINREQ);

    /*
     * Default is to use 4K pages, software can modify it
     * to any of the supported bits
     */
    pci_set_word(cfg + PCI_SRIOV_SYS_PGSIZE, 0x1);

    /* Set up device ID and initial/total number of VFs available */
    pci_set_word(cfg + PCI_SRIOV_VF_DID, vf_dev_id);
    pci_set_word(cfg + PCI_SRIOV_INITIAL_VF, init_vfs);
    pci_set_word(cfg + PCI_SRIOV_TOTAL_VF, total_vfs);
    pci_set_word(cfg + PCI_SRIOV_NUM_VF, 0);

    /* Write enable control bits */
    wmask = dev->wmask + offset;
    pci_set_word(wmask + PCI_SRIOV_CTRL,
                 PCI_SRIOV_CTRL_VFE | PCI_SRIOV_CTRL_MSE | PCI_SRIOV_CTRL_ARI);
    pci_set_word(wmask + PCI_SRIOV_NUM_VF, 0xffff);
    pci_set_word(wmask + PCI_SRIOV_SYS_PGSIZE, 0x553);

    qdev_prop_set_bit(&dev->qdev, "multifunction", true);

    dev->exp.sriov_pf.vf = g_new(PCIDevice *, total_vfs);

    for (uint16_t i = 0; i < total_vfs; i++) {
        PCIDevice *vf = pci_new(devfn, vfname);
        vf->exp.sriov_vf.pf = dev;
        vf->exp.sriov_vf.vf_number = i;

        if (!qdev_realize(&vf->qdev, bus, errp)) {
            object_unparent(OBJECT(vf));
            object_unref(vf);
            unparent_vfs(dev, i);
            return false;
        }

        /* set vid/did according to sr/iov spec - they are not used */
        pci_config_set_vendor_id(vf->config, 0xffff);
        pci_config_set_device_id(vf->config, 0xffff);

        dev->exp.sriov_pf.vf[i] = vf;
        devfn += vf_stride;
    }

    return true;
}

void pcie_sriov_pf_exit(PCIDevice *dev)
{
    uint8_t *cfg = dev->config + dev->exp.sriov_cap;

    unparent_vfs(dev, pci_get_word(cfg + PCI_SRIOV_TOTAL_VF));
}

void pcie_sriov_pf_init_vf_bar(PCIDevice *dev, int region_num,
                               uint8_t type, dma_addr_t size)
{
    uint32_t addr;
    uint64_t wmask;
    uint16_t sriov_cap = dev->exp.sriov_cap;

    assert(sriov_cap > 0);
    assert(region_num >= 0);
    assert(region_num < PCI_NUM_REGIONS);
    assert(region_num != PCI_ROM_SLOT);

    wmask = ~(size - 1);
    addr = sriov_cap + PCI_SRIOV_BAR + region_num * 4;

    pci_set_long(dev->config + addr, type);
    if (!(type & PCI_BASE_ADDRESS_SPACE_IO) &&
        type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
        pci_set_quad(dev->wmask + addr, wmask);
        pci_set_quad(dev->cmask + addr, ~0ULL);
    } else {
        pci_set_long(dev->wmask + addr, wmask & 0xffffffff);
        pci_set_long(dev->cmask + addr, 0xffffffff);
    }
    dev->exp.sriov_pf.vf_bar_type[region_num] = type;
}

void pcie_sriov_vf_register_bar(PCIDevice *dev, int region_num,
                                MemoryRegion *memory)
{
    PCIIORegion *r;
    PCIBus *bus = pci_get_bus(dev);
    uint8_t type;
    pcibus_t size = memory_region_size(memory);

    assert(pci_is_vf(dev)); /* PFs must use pci_register_bar */
    assert(region_num >= 0);
    assert(region_num < PCI_NUM_REGIONS);
    type = dev->exp.sriov_vf.pf->exp.sriov_pf.vf_bar_type[region_num];

    if (!is_power_of_2(size)) {
        error_report("%s: PCI region size must be a power"
                     " of two - type=0x%x, size=0x%"FMT_PCIBUS,
                     __func__, type, size);
        exit(1);
    }

    r = &dev->io_regions[region_num];
    r->memory = memory;
    r->address_space =
        type & PCI_BASE_ADDRESS_SPACE_IO
        ? bus->address_space_io
        : bus->address_space_mem;
    r->size = size;
    r->type = type;

    r->addr = pci_bar_address(dev, region_num, r->type, r->size);
    if (r->addr != PCI_BAR_UNMAPPED) {
        memory_region_add_subregion_overlap(r->address_space,
                                            r->addr, r->memory, 1);
    }
}

static void register_vfs(PCIDevice *dev)
{
    uint16_t num_vfs;
    uint16_t i;
    uint16_t sriov_cap = dev->exp.sriov_cap;

    assert(sriov_cap > 0);
    num_vfs = pci_get_word(dev->config + sriov_cap + PCI_SRIOV_NUM_VF);

    trace_sriov_register_vfs(dev->name, PCI_SLOT(dev->devfn),
                             PCI_FUNC(dev->devfn), num_vfs);
    for (i = 0; i < num_vfs; i++) {
        pci_set_enabled(dev->exp.sriov_pf.vf[i], true);
    }

    pci_set_word(dev->wmask + sriov_cap + PCI_SRIOV_NUM_VF, 0);
}

static void unregister_vfs(PCIDevice *dev)
{
    uint8_t *cfg = dev->config + dev->exp.sriov_cap;
    uint16_t i;

    trace_sriov_unregister_vfs(dev->name, PCI_SLOT(dev->devfn),
                               PCI_FUNC(dev->devfn));
    for (i = 0; i < pci_get_word(cfg + PCI_SRIOV_TOTAL_VF); i++) {
        pci_set_enabled(dev->exp.sriov_pf.vf[i], false);
    }

    pci_set_word(dev->wmask + dev->exp.sriov_cap + PCI_SRIOV_NUM_VF, 0xffff);
}

void pcie_sriov_config_write(PCIDevice *dev, uint32_t address,
                             uint32_t val, int len)
{
    uint32_t off;
    uint16_t sriov_cap = dev->exp.sriov_cap;

    if (!sriov_cap || address < sriov_cap) {
        return;
    }
    off = address - sriov_cap;
    if (off >= PCI_EXT_CAP_SRIOV_SIZEOF) {
        return;
    }

    trace_sriov_config_write(dev->name, PCI_SLOT(dev->devfn),
                             PCI_FUNC(dev->devfn), off, val, len);

    if (range_covers_byte(off, len, PCI_SRIOV_CTRL)) {
        if (val & PCI_SRIOV_CTRL_VFE) {
            register_vfs(dev);
        } else {
            unregister_vfs(dev);
        }
    } else if (range_covers_byte(off, len, PCI_SRIOV_NUM_VF)) {
        uint8_t *cfg = dev->config + sriov_cap;
        uint8_t *wmask = dev->wmask + sriov_cap;
        uint16_t num_vfs = pci_get_word(cfg + PCI_SRIOV_NUM_VF);
        uint16_t wmask_val = PCI_SRIOV_CTRL_MSE | PCI_SRIOV_CTRL_ARI;

        if (num_vfs <= pci_get_word(cfg + PCI_SRIOV_TOTAL_VF)) {
            wmask_val |= PCI_SRIOV_CTRL_VFE;
        }

        pci_set_word(wmask + PCI_SRIOV_CTRL, wmask_val);
    }
}

void pcie_sriov_pf_post_load(PCIDevice *dev)
{
    if (dev->exp.sriov_cap) {
        register_vfs(dev);
    }
}


/* Reset SR/IOV */
void pcie_sriov_pf_reset(PCIDevice *dev)
{
    uint16_t sriov_cap = dev->exp.sriov_cap;
    if (!sriov_cap) {
        return;
    }

    pci_set_word(dev->config + sriov_cap + PCI_SRIOV_CTRL, 0);
    unregister_vfs(dev);

    pci_set_word(dev->config + sriov_cap + PCI_SRIOV_NUM_VF, 0);
    pci_set_word(dev->wmask + sriov_cap + PCI_SRIOV_CTRL,
                 PCI_SRIOV_CTRL_VFE | PCI_SRIOV_CTRL_MSE | PCI_SRIOV_CTRL_ARI);

    /*
     * Default is to use 4K pages, software can modify it
     * to any of the supported bits
     */
    pci_set_word(dev->config + sriov_cap + PCI_SRIOV_SYS_PGSIZE, 0x1);

    for (uint16_t i = 0; i < PCI_NUM_REGIONS; i++) {
        pci_set_quad(dev->config + sriov_cap + PCI_SRIOV_BAR + i * 4,
                     dev->exp.sriov_pf.vf_bar_type[i]);
    }
}

/* Add optional supported page sizes to the mask of supported page sizes */
void pcie_sriov_pf_add_sup_pgsize(PCIDevice *dev, uint16_t opt_sup_pgsize)
{
    uint8_t *cfg = dev->config + dev->exp.sriov_cap;
    uint8_t *wmask = dev->wmask + dev->exp.sriov_cap;

    uint16_t sup_pgsize = pci_get_word(cfg + PCI_SRIOV_SUP_PGSIZE);

    sup_pgsize |= opt_sup_pgsize;

    /*
     * Make sure the new bits are set, and that system page size
     * also can be set to any of the new values according to spec:
     */
    pci_set_word(cfg + PCI_SRIOV_SUP_PGSIZE, sup_pgsize);
    pci_set_word(wmask + PCI_SRIOV_SYS_PGSIZE, sup_pgsize);
}


uint16_t pcie_sriov_vf_number(PCIDevice *dev)
{
    assert(pci_is_vf(dev));
    return dev->exp.sriov_vf.vf_number;
}

PCIDevice *pcie_sriov_get_pf(PCIDevice *dev)
{
    return dev->exp.sriov_vf.pf;
}

PCIDevice *pcie_sriov_get_vf_at_index(PCIDevice *dev, int n)
{
    assert(!pci_is_vf(dev));
    if (n < pcie_sriov_num_vfs(dev)) {
        return dev->exp.sriov_pf.vf[n];
    }
    return NULL;
}

uint16_t pcie_sriov_num_vfs(PCIDevice *dev)
{
    uint16_t sriov_cap = dev->exp.sriov_cap;
    uint8_t *cfg = dev->config + sriov_cap;

    return sriov_cap &&
           (pci_get_word(cfg + PCI_SRIOV_CTRL) & PCI_SRIOV_CTRL_VFE) ?
           pci_get_word(cfg + PCI_SRIOV_NUM_VF) : 0;
}
