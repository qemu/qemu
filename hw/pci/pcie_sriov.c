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
#include "qemu/range.h"
#include "qapi/error.h"
#include "trace.h"

static GHashTable *pfs;

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

static void consume_config(PCIDevice *dev)
{
    uint8_t *cfg = dev->config + dev->exp.sriov_cap;

    if (pci_get_word(cfg + PCI_SRIOV_CTRL) & PCI_SRIOV_CTRL_VFE) {
        register_vfs(dev);
    } else {
        uint8_t *wmask = dev->wmask + dev->exp.sriov_cap;
        uint16_t num_vfs = pci_get_word(cfg + PCI_SRIOV_NUM_VF);
        uint16_t wmask_val = PCI_SRIOV_CTRL_MSE | PCI_SRIOV_CTRL_ARI;

        unregister_vfs(dev);

        if (num_vfs <= pci_get_word(cfg + PCI_SRIOV_TOTAL_VF)) {
            wmask_val |= PCI_SRIOV_CTRL_VFE;
        }

        pci_set_word(wmask + PCI_SRIOV_CTRL, wmask_val);
    }
}

static bool pcie_sriov_pf_init_common(PCIDevice *dev, uint16_t offset,
                                      uint16_t vf_dev_id, uint16_t init_vfs,
                                      uint16_t total_vfs, uint16_t vf_offset,
                                      uint16_t vf_stride, Error **errp)
{
    int32_t devfn = dev->devfn + vf_offset;
    uint8_t *cfg = dev->config + offset;
    uint8_t *wmask;

    if (!pci_is_express(dev)) {
        error_setg(errp, "PCI Express is required for SR-IOV PF");
        return false;
    }

    if (pci_is_vf(dev)) {
        error_setg(errp, "a device cannot be a SR-IOV PF and a VF at the same time");
        return false;
    }

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

    return true;
}

bool pcie_sriov_pf_init(PCIDevice *dev, uint16_t offset,
                        const char *vfname, uint16_t vf_dev_id,
                        uint16_t init_vfs, uint16_t total_vfs,
                        uint16_t vf_offset, uint16_t vf_stride,
                        Error **errp)
{
    BusState *bus = qdev_get_parent_bus(&dev->qdev);
    int32_t devfn = dev->devfn + vf_offset;

    if (pfs && g_hash_table_contains(pfs, dev->qdev.id)) {
        error_setg(errp, "attaching user-created SR-IOV VF unsupported");
        return false;
    }

    if (!pcie_sriov_pf_init_common(dev, offset, vf_dev_id, init_vfs,
                                   total_vfs, vf_offset, vf_stride, errp)) {
        return false;
    }

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
    if (dev->exp.sriov_cap == 0) {
        return;
    }

    if (dev->exp.sriov_pf.vf_user_created) {
        uint16_t ven_id = pci_get_word(dev->config + PCI_VENDOR_ID);
        uint16_t total_vfs = pci_get_word(dev->config + PCI_SRIOV_TOTAL_VF);
        uint16_t vf_dev_id = pci_get_word(dev->config + PCI_SRIOV_VF_DID);

        unregister_vfs(dev);

        for (uint16_t i = 0; i < total_vfs; i++) {
            dev->exp.sriov_pf.vf[i]->exp.sriov_vf.pf = NULL;

            pci_config_set_vendor_id(dev->exp.sriov_pf.vf[i]->config, ven_id);
            pci_config_set_device_id(dev->exp.sriov_pf.vf[i]->config, vf_dev_id);
        }
    } else {
        uint8_t *cfg = dev->config + dev->exp.sriov_cap;

        unparent_vfs(dev, pci_get_word(cfg + PCI_SRIOV_TOTAL_VF));
    }
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

static gint compare_vf_devfns(gconstpointer a, gconstpointer b)
{
    return (*(PCIDevice **)a)->devfn - (*(PCIDevice **)b)->devfn;
}

int16_t pcie_sriov_pf_init_from_user_created_vfs(PCIDevice *dev,
                                                 uint16_t offset,
                                                 Error **errp)
{
    GPtrArray *pf;
    PCIDevice **vfs;
    BusState *bus = qdev_get_parent_bus(DEVICE(dev));
    uint16_t ven_id = pci_get_word(dev->config + PCI_VENDOR_ID);
    uint16_t size = PCI_EXT_CAP_SRIOV_SIZEOF;
    uint16_t vf_dev_id;
    uint16_t vf_offset;
    uint16_t vf_stride;
    uint16_t i;

    if (!pfs || !dev->qdev.id) {
        return 0;
    }

    pf = g_hash_table_lookup(pfs, dev->qdev.id);
    if (!pf) {
        return 0;
    }

    if (pf->len > UINT16_MAX) {
        error_setg(errp, "too many VFs");
        return -1;
    }

    g_ptr_array_sort(pf, compare_vf_devfns);
    vfs = (void *)pf->pdata;

    if (vfs[0]->devfn <= dev->devfn) {
        error_setg(errp, "a VF function number is less than the PF function number");
        return -1;
    }

    vf_dev_id = pci_get_word(vfs[0]->config + PCI_DEVICE_ID);
    vf_offset = vfs[0]->devfn - dev->devfn;
    vf_stride = pf->len < 2 ? 0 : vfs[1]->devfn - vfs[0]->devfn;

    for (i = 0; i < pf->len; i++) {
        if (bus != qdev_get_parent_bus(&vfs[i]->qdev)) {
            error_setg(errp, "SR-IOV VF parent bus mismatches with PF");
            return -1;
        }

        if (ven_id != pci_get_word(vfs[i]->config + PCI_VENDOR_ID)) {
            error_setg(errp, "SR-IOV VF vendor ID mismatches with PF");
            return -1;
        }

        if (vf_dev_id != pci_get_word(vfs[i]->config + PCI_DEVICE_ID)) {
            error_setg(errp, "inconsistent SR-IOV VF device IDs");
            return -1;
        }

        for (size_t j = 0; j < PCI_NUM_REGIONS; j++) {
            if (vfs[i]->io_regions[j].size != vfs[0]->io_regions[j].size ||
                vfs[i]->io_regions[j].type != vfs[0]->io_regions[j].type) {
                error_setg(errp, "inconsistent SR-IOV BARs");
                return -1;
            }
        }

        if (vfs[i]->devfn - vfs[0]->devfn != vf_stride * i) {
            error_setg(errp, "inconsistent SR-IOV stride");
            return -1;
        }
    }

    if (!pcie_sriov_pf_init_common(dev, offset, vf_dev_id, pf->len,
                                   pf->len, vf_offset, vf_stride, errp)) {
        return -1;
    }

    if (!pcie_find_capability(dev, PCI_EXT_CAP_ID_ARI)) {
        pcie_ari_init(dev, offset + size);
        size += PCI_ARI_SIZEOF;
    }

    for (i = 0; i < pf->len; i++) {
        vfs[i]->exp.sriov_vf.pf = dev;
        vfs[i]->exp.sriov_vf.vf_number = i;

        /* set vid/did according to sr/iov spec - they are not used */
        pci_config_set_vendor_id(vfs[i]->config, 0xffff);
        pci_config_set_device_id(vfs[i]->config, 0xffff);
    }

    dev->exp.sriov_pf.vf = vfs;
    dev->exp.sriov_pf.vf_user_created = true;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        PCIIORegion *region = &vfs[0]->io_regions[i];

        if (region->size) {
            pcie_sriov_pf_init_vf_bar(dev, i, region->type, region->size);
        }
    }

    return size;
}

bool pcie_sriov_register_device(PCIDevice *dev, Error **errp)
{
    if (!dev->exp.sriov_pf.vf && dev->qdev.id &&
        pfs && g_hash_table_contains(pfs, dev->qdev.id)) {
        error_setg(errp, "attaching user-created SR-IOV VF unsupported");
        return false;
    }

    if (dev->sriov_pf) {
        PCIDevice *pci_pf;
        GPtrArray *pf;

        if (!PCI_DEVICE_GET_CLASS(dev)->sriov_vf_user_creatable) {
            error_setg(errp, "user cannot create SR-IOV VF with this device type");
            return false;
        }

        if (!pci_is_express(dev)) {
            error_setg(errp, "PCI Express is required for SR-IOV VF");
            return false;
        }

        if (!pci_qdev_find_device(dev->sriov_pf, &pci_pf)) {
            error_setg(errp, "PCI device specified as SR-IOV PF already exists");
            return false;
        }

        if (!pfs) {
            pfs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        }

        pf = g_hash_table_lookup(pfs, dev->sriov_pf);
        if (!pf) {
            pf = g_ptr_array_new();
            g_hash_table_insert(pfs, g_strdup(dev->sriov_pf), pf);
        }

        g_ptr_array_add(pf, dev);
    }

    return true;
}

void pcie_sriov_unregister_device(PCIDevice *dev)
{
    if (dev->sriov_pf && pfs) {
        GPtrArray *pf = g_hash_table_lookup(pfs, dev->sriov_pf);

        if (pf) {
            g_ptr_array_remove_fast(pf, dev);

            if (!pf->len) {
                g_hash_table_remove(pfs, dev->sriov_pf);
                g_ptr_array_free(pf, FALSE);
            }
        }
    }
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

    consume_config(dev);
}

void pcie_sriov_pf_post_load(PCIDevice *dev)
{
    if (dev->exp.sriov_cap) {
        consume_config(dev);
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
    assert(dev->exp.sriov_vf.pf);
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
