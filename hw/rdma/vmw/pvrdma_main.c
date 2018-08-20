/*
 * QEMU paravirtual RDMA
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "cpu.h"
#include "trace.h"

#include "../rdma_rm.h"
#include "../rdma_backend.h"
#include "../rdma_utils.h"

#include <infiniband/verbs.h>
#include "pvrdma.h"
#include "standard-headers/rdma/vmw_pvrdma-abi.h"
#include "standard-headers/drivers/infiniband/hw/vmw_pvrdma/pvrdma_dev_api.h"
#include "pvrdma_qp_ops.h"

static Property pvrdma_dev_properties[] = {
    DEFINE_PROP_STRING("backend-dev", PVRDMADev, backend_device_name),
    DEFINE_PROP_UINT8("backend-port", PVRDMADev, backend_port_num, 1),
    DEFINE_PROP_UINT8("backend-gid-idx", PVRDMADev, backend_gid_idx, 0),
    DEFINE_PROP_UINT64("dev-caps-max-mr-size", PVRDMADev, dev_attr.max_mr_size,
                       MAX_MR_SIZE),
    DEFINE_PROP_INT32("dev-caps-max-qp", PVRDMADev, dev_attr.max_qp, MAX_QP),
    DEFINE_PROP_INT32("dev-caps-max-sge", PVRDMADev, dev_attr.max_sge, MAX_SGE),
    DEFINE_PROP_INT32("dev-caps-max-cq", PVRDMADev, dev_attr.max_cq, MAX_CQ),
    DEFINE_PROP_INT32("dev-caps-max-mr", PVRDMADev, dev_attr.max_mr, MAX_MR),
    DEFINE_PROP_INT32("dev-caps-max-pd", PVRDMADev, dev_attr.max_pd, MAX_PD),
    DEFINE_PROP_INT32("dev-caps-qp-rd-atom", PVRDMADev, dev_attr.max_qp_rd_atom,
                      MAX_QP_RD_ATOM),
    DEFINE_PROP_INT32("dev-caps-max-qp-init-rd-atom", PVRDMADev,
                      dev_attr.max_qp_init_rd_atom, MAX_QP_INIT_RD_ATOM),
    DEFINE_PROP_INT32("dev-caps-max-ah", PVRDMADev, dev_attr.max_ah, MAX_AH),
    DEFINE_PROP_END_OF_LIST(),
};

static void free_dev_ring(PCIDevice *pci_dev, PvrdmaRing *ring,
                          void *ring_state)
{
    pvrdma_ring_free(ring);
    rdma_pci_dma_unmap(pci_dev, ring_state, TARGET_PAGE_SIZE);
}

static int init_dev_ring(PvrdmaRing *ring, struct pvrdma_ring **ring_state,
                         const char *name, PCIDevice *pci_dev,
                         dma_addr_t dir_addr, uint32_t num_pages)
{
    uint64_t *dir, *tbl;
    int rc = 0;

    pr_dbg("Initializing device ring %s\n", name);
    pr_dbg("pdir_dma=0x%llx\n", (long long unsigned int)dir_addr);
    pr_dbg("num_pages=%d\n", num_pages);
    dir = rdma_pci_dma_map(pci_dev, dir_addr, TARGET_PAGE_SIZE);
    if (!dir) {
        pr_err("Failed to map to page directory\n");
        rc = -ENOMEM;
        goto out;
    }
    tbl = rdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        pr_err("Failed to map to page table\n");
        rc = -ENOMEM;
        goto out_free_dir;
    }

    *ring_state = rdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!*ring_state) {
        pr_err("Failed to map to ring state\n");
        rc = -ENOMEM;
        goto out_free_tbl;
    }
    /* RX ring is the second */
    (*ring_state)++;
    rc = pvrdma_ring_init(ring, name, pci_dev,
                          (struct pvrdma_ring *)*ring_state,
                          (num_pages - 1) * TARGET_PAGE_SIZE /
                          sizeof(struct pvrdma_cqne),
                          sizeof(struct pvrdma_cqne),
                          (dma_addr_t *)&tbl[1], (dma_addr_t)num_pages - 1);
    if (rc) {
        pr_err("Failed to initialize ring\n");
        rc = -ENOMEM;
        goto out_free_ring_state;
    }

    goto out_free_tbl;

out_free_ring_state:
    rdma_pci_dma_unmap(pci_dev, *ring_state, TARGET_PAGE_SIZE);

out_free_tbl:
    rdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);

out_free_dir:
    rdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);

out:
    return rc;
}

static void free_dsr(PVRDMADev *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);

    if (!dev->dsr_info.dsr) {
        return;
    }

    free_dev_ring(pci_dev, &dev->dsr_info.async,
                  dev->dsr_info.async_ring_state);

    free_dev_ring(pci_dev, &dev->dsr_info.cq, dev->dsr_info.cq_ring_state);

    rdma_pci_dma_unmap(pci_dev, dev->dsr_info.req,
                         sizeof(union pvrdma_cmd_req));

    rdma_pci_dma_unmap(pci_dev, dev->dsr_info.rsp,
                         sizeof(union pvrdma_cmd_resp));

    rdma_pci_dma_unmap(pci_dev, dev->dsr_info.dsr,
                         sizeof(struct pvrdma_device_shared_region));

    dev->dsr_info.dsr = NULL;
}

static int load_dsr(PVRDMADev *dev)
{
    int rc = 0;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    DSRInfo *dsr_info;
    struct pvrdma_device_shared_region *dsr;

    free_dsr(dev);

    /* Map to DSR */
    pr_dbg("dsr_dma=0x%llx\n", (long long unsigned int)dev->dsr_info.dma);
    dev->dsr_info.dsr = rdma_pci_dma_map(pci_dev, dev->dsr_info.dma,
                              sizeof(struct pvrdma_device_shared_region));
    if (!dev->dsr_info.dsr) {
        pr_err("Failed to map to DSR\n");
        rc = -ENOMEM;
        goto out;
    }

    /* Shortcuts */
    dsr_info = &dev->dsr_info;
    dsr = dsr_info->dsr;

    /* Map to command slot */
    pr_dbg("cmd_dma=0x%llx\n", (long long unsigned int)dsr->cmd_slot_dma);
    dsr_info->req = rdma_pci_dma_map(pci_dev, dsr->cmd_slot_dma,
                                     sizeof(union pvrdma_cmd_req));
    if (!dsr_info->req) {
        pr_err("Failed to map to command slot address\n");
        rc = -ENOMEM;
        goto out_free_dsr;
    }

    /* Map to response slot */
    pr_dbg("rsp_dma=0x%llx\n", (long long unsigned int)dsr->resp_slot_dma);
    dsr_info->rsp = rdma_pci_dma_map(pci_dev, dsr->resp_slot_dma,
                                     sizeof(union pvrdma_cmd_resp));
    if (!dsr_info->rsp) {
        pr_err("Failed to map to response slot address\n");
        rc = -ENOMEM;
        goto out_free_req;
    }

    /* Map to CQ notification ring */
    rc = init_dev_ring(&dsr_info->cq, &dsr_info->cq_ring_state, "dev_cq",
                       pci_dev, dsr->cq_ring_pages.pdir_dma,
                       dsr->cq_ring_pages.num_pages);
    if (rc) {
        pr_err("Failed to map to initialize CQ ring\n");
        rc = -ENOMEM;
        goto out_free_rsp;
    }

    /* Map to event notification ring */
    rc = init_dev_ring(&dsr_info->async, &dsr_info->async_ring_state,
                       "dev_async", pci_dev, dsr->async_ring_pages.pdir_dma,
                       dsr->async_ring_pages.num_pages);
    if (rc) {
        pr_err("Failed to map to initialize event ring\n");
        rc = -ENOMEM;
        goto out_free_rsp;
    }

    goto out;

out_free_rsp:
    rdma_pci_dma_unmap(pci_dev, dsr_info->rsp, sizeof(union pvrdma_cmd_resp));

out_free_req:
    rdma_pci_dma_unmap(pci_dev, dsr_info->req, sizeof(union pvrdma_cmd_req));

out_free_dsr:
    rdma_pci_dma_unmap(pci_dev, dsr_info->dsr,
                       sizeof(struct pvrdma_device_shared_region));
    dsr_info->dsr = NULL;

out:
    return rc;
}

static void init_dsr_dev_caps(PVRDMADev *dev)
{
    struct pvrdma_device_shared_region *dsr;

    if (dev->dsr_info.dsr == NULL) {
        pr_err("Can't initialized DSR\n");
        return;
    }

    dsr = dev->dsr_info.dsr;

    dsr->caps.fw_ver = PVRDMA_FW_VERSION;
    pr_dbg("fw_ver=0x%" PRIx64 "\n", dsr->caps.fw_ver);

    dsr->caps.mode = PVRDMA_DEVICE_MODE_ROCE;
    pr_dbg("mode=%d\n", dsr->caps.mode);

    dsr->caps.gid_types |= PVRDMA_GID_TYPE_FLAG_ROCE_V1;
    pr_dbg("gid_types=0x%x\n", dsr->caps.gid_types);

    dsr->caps.max_uar = RDMA_BAR2_UAR_SIZE;
    pr_dbg("max_uar=%d\n", dsr->caps.max_uar);

    dsr->caps.max_mr_size = dev->dev_attr.max_mr_size;
    dsr->caps.max_qp = dev->dev_attr.max_qp;
    dsr->caps.max_qp_wr = dev->dev_attr.max_qp_wr;
    dsr->caps.max_sge = dev->dev_attr.max_sge;
    dsr->caps.max_cq = dev->dev_attr.max_cq;
    dsr->caps.max_cqe = dev->dev_attr.max_cqe;
    dsr->caps.max_mr = dev->dev_attr.max_mr;
    dsr->caps.max_pd = dev->dev_attr.max_pd;
    dsr->caps.max_ah = dev->dev_attr.max_ah;

    dsr->caps.gid_tbl_len = MAX_GIDS;
    pr_dbg("gid_tbl_len=%d\n", dsr->caps.gid_tbl_len);

    dsr->caps.sys_image_guid = 0;
    pr_dbg("sys_image_guid=%" PRIx64 "\n", dsr->caps.sys_image_guid);

    dsr->caps.node_guid = cpu_to_be64(dev->node_guid);
    pr_dbg("node_guid=%" PRIx64 "\n", be64_to_cpu(dsr->caps.node_guid));

    dsr->caps.phys_port_cnt = MAX_PORTS;
    pr_dbg("phys_port_cnt=%d\n", dsr->caps.phys_port_cnt);

    dsr->caps.max_pkeys = MAX_PKEYS;
    pr_dbg("max_pkeys=%d\n", dsr->caps.max_pkeys);

    pr_dbg("Initialized\n");
}

static void init_ports(PVRDMADev *dev, Error **errp)
{
    int i;

    memset(dev->rdma_dev_res.ports, 0, sizeof(dev->rdma_dev_res.ports));

    for (i = 0; i < MAX_PORTS; i++) {
        dev->rdma_dev_res.ports[i].state = IBV_PORT_DOWN;
    }
}

static void uninit_msix(PCIDevice *pdev, int used_vectors)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    int i;

    for (i = 0; i < used_vectors; i++) {
        msix_vector_unuse(pdev, i);
    }

    msix_uninit(pdev, &dev->msix, &dev->msix);
}

static int init_msix(PCIDevice *pdev, Error **errp)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    int i;
    int rc;

    rc = msix_init(pdev, RDMA_MAX_INTRS, &dev->msix, RDMA_MSIX_BAR_IDX,
                   RDMA_MSIX_TABLE, &dev->msix, RDMA_MSIX_BAR_IDX,
                   RDMA_MSIX_PBA, 0, NULL);

    if (rc < 0) {
        error_setg(errp, "Failed to initialize MSI-X");
        return rc;
    }

    for (i = 0; i < RDMA_MAX_INTRS; i++) {
        rc = msix_vector_use(PCI_DEVICE(dev), i);
        if (rc < 0) {
            error_setg(errp, "Fail mark MSI-X vector %d", i);
            uninit_msix(pdev, i);
            return rc;
        }
    }

    return 0;
}

static void pvrdma_fini(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    pr_dbg("Closing device %s %x.%x\n", pdev->name, PCI_SLOT(pdev->devfn),
           PCI_FUNC(pdev->devfn));

    pvrdma_qp_ops_fini();

    rdma_rm_fini(&dev->rdma_dev_res);

    rdma_backend_fini(&dev->backend_dev);

    free_dsr(dev);

    if (msix_enabled(pdev)) {
        uninit_msix(pdev, RDMA_MAX_INTRS);
    }
}

static void pvrdma_stop(PVRDMADev *dev)
{
    rdma_backend_stop(&dev->backend_dev);
}

static void pvrdma_start(PVRDMADev *dev)
{
    rdma_backend_start(&dev->backend_dev);
}

static void activate_device(PVRDMADev *dev)
{
    pvrdma_start(dev);
    set_reg_val(dev, PVRDMA_REG_ERR, 0);
    pr_dbg("Device activated\n");
}

static int unquiesce_device(PVRDMADev *dev)
{
    pr_dbg("Device unquiesced\n");
    return 0;
}

static int reset_device(PVRDMADev *dev)
{
    pvrdma_stop(dev);

    pr_dbg("Device reset complete\n");

    return 0;
}

static uint64_t regs_read(void *opaque, hwaddr addr, unsigned size)
{
    PVRDMADev *dev = opaque;
    uint32_t val;

    /* pr_dbg("addr=0x%lx, size=%d\n", addr, size); */

    if (get_reg_val(dev, addr, &val)) {
        pr_dbg("Error trying to read REG value from address 0x%x\n",
               (uint32_t)addr);
        return -EINVAL;
    }

    trace_pvrdma_regs_read(addr, val);

    return val;
}

static void regs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PVRDMADev *dev = opaque;

    /* pr_dbg("addr=0x%lx, val=0x%x, size=%d\n", addr, (uint32_t)val, size); */

    if (set_reg_val(dev, addr, val)) {
        pr_err("Fail to set REG value, addr=0x%" PRIx64 ", val=0x%" PRIx64 "\n",
               addr, val);
        return;
    }

    trace_pvrdma_regs_write(addr, val);

    switch (addr) {
    case PVRDMA_REG_DSRLOW:
        dev->dsr_info.dma = val;
        break;
    case PVRDMA_REG_DSRHIGH:
        dev->dsr_info.dma |= val << 32;
        load_dsr(dev);
        init_dsr_dev_caps(dev);
        break;
    case PVRDMA_REG_CTL:
        switch (val) {
        case PVRDMA_DEVICE_CTL_ACTIVATE:
            activate_device(dev);
            break;
        case PVRDMA_DEVICE_CTL_UNQUIESCE:
            unquiesce_device(dev);
            break;
        case PVRDMA_DEVICE_CTL_RESET:
            reset_device(dev);
            break;
        }
        break;
    case PVRDMA_REG_IMR:
        pr_dbg("Interrupt mask=0x%" PRIx64 "\n", val);
        dev->interrupt_mask = val;
        break;
    case PVRDMA_REG_REQUEST:
        if (val == 0) {
            execute_command(dev);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps regs_ops = {
    .read = regs_read,
    .write = regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = sizeof(uint32_t),
        .max_access_size = sizeof(uint32_t),
    },
};

static void uar_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PVRDMADev *dev = opaque;

    /* pr_dbg("addr=0x%lx, val=0x%x, size=%d\n", addr, (uint32_t)val, size); */

    switch (addr & 0xFFF) { /* Mask with 0xFFF as each UC gets page */
    case PVRDMA_UAR_QP_OFFSET:
        pr_dbg("UAR QP command, addr=0x%" PRIx64 ", val=0x%" PRIx64 "\n",
               (uint64_t)addr, val);
        if (val & PVRDMA_UAR_QP_SEND) {
            pvrdma_qp_send(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        if (val & PVRDMA_UAR_QP_RECV) {
            pvrdma_qp_recv(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        break;
    case PVRDMA_UAR_CQ_OFFSET:
        /* pr_dbg("UAR CQ cmd, addr=0x%x, val=0x%lx\n", (uint32_t)addr, val); */
        if (val & PVRDMA_UAR_CQ_ARM) {
            rdma_rm_req_notify_cq(&dev->rdma_dev_res,
                                  val & PVRDMA_UAR_HANDLE_MASK,
                                  !!(val & PVRDMA_UAR_CQ_ARM_SOL));
        }
        if (val & PVRDMA_UAR_CQ_ARM_SOL) {
            pr_dbg("UAR_CQ_ARM_SOL (%" PRIx64 ")\n",
                   val & PVRDMA_UAR_HANDLE_MASK);
        }
        if (val & PVRDMA_UAR_CQ_POLL) {
            pr_dbg("UAR_CQ_POLL (%" PRIx64 ")\n", val & PVRDMA_UAR_HANDLE_MASK);
            pvrdma_cq_poll(&dev->rdma_dev_res, val & PVRDMA_UAR_HANDLE_MASK);
        }
        break;
    default:
        pr_err("Unsupported command, addr=0x%" PRIx64 ", val=0x%" PRIx64 "\n",
               addr, val);
        break;
    }
}

static const MemoryRegionOps uar_ops = {
    .write = uar_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = sizeof(uint32_t),
        .max_access_size = sizeof(uint32_t),
    },
};

static void init_pci_config(PCIDevice *pdev)
{
    pdev->config[PCI_INTERRUPT_PIN] = 1;
}

static void init_bars(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    /* BAR 0 - MSI-X */
    memory_region_init(&dev->msix, OBJECT(dev), "pvrdma-msix",
                       RDMA_BAR0_MSIX_SIZE);
    pci_register_bar(pdev, RDMA_MSIX_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->msix);

    /* BAR 1 - Registers */
    memset(&dev->regs_data, 0, sizeof(dev->regs_data));
    memory_region_init_io(&dev->regs, OBJECT(dev), &regs_ops, dev,
                          "pvrdma-regs", sizeof(dev->regs_data));
    pci_register_bar(pdev, RDMA_REG_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->regs);

    /* BAR 2 - UAR */
    memset(&dev->uar_data, 0, sizeof(dev->uar_data));
    memory_region_init_io(&dev->uar, OBJECT(dev), &uar_ops, dev, "rdma-uar",
                          sizeof(dev->uar_data));
    pci_register_bar(pdev, RDMA_UAR_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->uar);
}

static void init_regs(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    set_reg_val(dev, PVRDMA_REG_VERSION, PVRDMA_HW_VERSION);
    set_reg_val(dev, PVRDMA_REG_ERR, 0xFFFF);
}

static void init_dev_caps(PVRDMADev *dev)
{
    size_t pg_tbl_bytes = TARGET_PAGE_SIZE *
                          (TARGET_PAGE_SIZE / sizeof(uint64_t));
    size_t wr_sz = MAX(sizeof(struct pvrdma_sq_wqe_hdr),
                       sizeof(struct pvrdma_rq_wqe_hdr));

    dev->dev_attr.max_qp_wr = pg_tbl_bytes /
                              (wr_sz + sizeof(struct pvrdma_sge) * MAX_SGE) -
                              TARGET_PAGE_SIZE; /* First page is ring state */
    pr_dbg("max_qp_wr=%d\n", dev->dev_attr.max_qp_wr);

    dev->dev_attr.max_cqe = pg_tbl_bytes / sizeof(struct pvrdma_cqe) -
                            TARGET_PAGE_SIZE; /* First page is ring state */
    pr_dbg("max_cqe=%d\n", dev->dev_attr.max_cqe);
}

static int pvrdma_check_ram_shared(Object *obj, void *opaque)
{
    bool *shared = opaque;

    if (object_dynamic_cast(obj, "memory-backend-ram")) {
        *shared = object_property_get_bool(obj, "share", NULL);
    }

    return 0;
}

static void pvrdma_realize(PCIDevice *pdev, Error **errp)
{
    int rc;
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    Object *memdev_root;
    bool ram_shared = false;

    init_pr_dbg();

    pr_dbg("Initializing device %s %x.%x\n", pdev->name,
           PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

    if (TARGET_PAGE_SIZE != getpagesize()) {
        error_setg(errp, "Target page size must be the same as host page size");
        return;
    }

    memdev_root = object_resolve_path("/objects", NULL);
    if (memdev_root) {
        object_child_foreach(memdev_root, pvrdma_check_ram_shared, &ram_shared);
    }
    if (!ram_shared) {
        error_setg(errp, "Only shared memory backed ram is supported");
        return;
    }

    dev->dsr_info.dsr = NULL;

    init_pci_config(pdev);

    init_bars(pdev);

    init_regs(pdev);

    init_dev_caps(dev);

    rc = init_msix(pdev, errp);
    if (rc) {
        goto out;
    }

    rc = rdma_backend_init(&dev->backend_dev, pdev, &dev->rdma_dev_res,
                           dev->backend_device_name, dev->backend_port_num,
                           dev->backend_gid_idx, &dev->dev_attr, errp);
    if (rc) {
        goto out;
    }

    rc = rdma_rm_init(&dev->rdma_dev_res, &dev->dev_attr, errp);
    if (rc) {
        goto out;
    }

    init_ports(dev, errp);

    rc = pvrdma_qp_ops_init();
    if (rc) {
        goto out;
    }

out:
    if (rc) {
        error_append_hint(errp, "Device fail to load\n");
    }
}

static void pvrdma_exit(PCIDevice *pdev)
{
    pvrdma_fini(pdev);
}

static void pvrdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pvrdma_realize;
    k->exit = pvrdma_exit;
    k->vendor_id = PCI_VENDOR_ID_VMWARE;
    k->device_id = PCI_DEVICE_ID_VMWARE_PVRDMA;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_OTHER;

    dc->desc = "RDMA Device";
    dc->props = pvrdma_dev_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pvrdma_info = {
    .name = PVRDMA_HW_NAME,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PVRDMADev),
    .class_init = pvrdma_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&pvrdma_info);
}

type_init(register_types)
