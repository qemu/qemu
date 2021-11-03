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
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "cpu.h"
#include "trace.h"
#include "monitor/monitor.h"
#include "hw/rdma/rdma.h"

#include "../rdma_rm.h"
#include "../rdma_backend.h"
#include "../rdma_utils.h"

#include <infiniband/verbs.h>
#include "pvrdma.h"
#include "standard-headers/rdma/vmw_pvrdma-abi.h"
#include "sysemu/runstate.h"
#include "standard-headers/drivers/infiniband/hw/vmw_pvrdma/pvrdma_dev_api.h"
#include "pvrdma_qp_ops.h"

static Property pvrdma_dev_properties[] = {
    DEFINE_PROP_STRING("netdev", PVRDMADev, backend_eth_device_name),
    DEFINE_PROP_STRING("ibdev", PVRDMADev, backend_device_name),
    DEFINE_PROP_UINT8("ibport", PVRDMADev, backend_port_num, 1),
    DEFINE_PROP_UINT64("dev-caps-max-mr-size", PVRDMADev, dev_attr.max_mr_size,
                       MAX_MR_SIZE),
    DEFINE_PROP_INT32("dev-caps-max-qp", PVRDMADev, dev_attr.max_qp, MAX_QP),
    DEFINE_PROP_INT32("dev-caps-max-cq", PVRDMADev, dev_attr.max_cq, MAX_CQ),
    DEFINE_PROP_INT32("dev-caps-max-mr", PVRDMADev, dev_attr.max_mr, MAX_MR),
    DEFINE_PROP_INT32("dev-caps-max-pd", PVRDMADev, dev_attr.max_pd, MAX_PD),
    DEFINE_PROP_INT32("dev-caps-qp-rd-atom", PVRDMADev, dev_attr.max_qp_rd_atom,
                      MAX_QP_RD_ATOM),
    DEFINE_PROP_INT32("dev-caps-max-qp-init-rd-atom", PVRDMADev,
                      dev_attr.max_qp_init_rd_atom, MAX_QP_INIT_RD_ATOM),
    DEFINE_PROP_INT32("dev-caps-max-ah", PVRDMADev, dev_attr.max_ah, MAX_AH),
    DEFINE_PROP_INT32("dev-caps-max-srq", PVRDMADev, dev_attr.max_srq, MAX_SRQ),
    DEFINE_PROP_CHR("mad-chardev", PVRDMADev, mad_chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvrdma_format_statistics(RdmaProvider *obj, GString *buf)
{
    PVRDMADev *dev = PVRDMA_DEV(obj);
    PCIDevice *pdev = PCI_DEVICE(dev);

    g_string_append_printf(buf, "%s, %x.%x\n",
                           pdev->name, PCI_SLOT(pdev->devfn),
                           PCI_FUNC(pdev->devfn));
    g_string_append_printf(buf, "\tcommands         : %" PRId64 "\n",
                           dev->stats.commands);
    g_string_append_printf(buf, "\tregs_reads       : %" PRId64 "\n",
                           dev->stats.regs_reads);
    g_string_append_printf(buf, "\tregs_writes      : %" PRId64 "\n",
                           dev->stats.regs_writes);
    g_string_append_printf(buf, "\tuar_writes       : %" PRId64 "\n",
                           dev->stats.uar_writes);
    g_string_append_printf(buf, "\tinterrupts       : %" PRId64 "\n",
                           dev->stats.interrupts);
    rdma_format_device_counters(&dev->rdma_dev_res, buf);
}

static void free_dev_ring(PCIDevice *pci_dev, PvrdmaRing *ring,
                          void *ring_state)
{
    pvrdma_ring_free(ring);
    rdma_pci_dma_unmap(pci_dev, ring_state, TARGET_PAGE_SIZE);
}

static int init_dev_ring(PvrdmaRing *ring, PvrdmaRingState **ring_state,
                         const char *name, PCIDevice *pci_dev,
                         dma_addr_t dir_addr, uint32_t num_pages)
{
    uint64_t *dir, *tbl;
    int rc = 0;

    if (!num_pages) {
        rdma_error_report("Ring pages count must be strictly positive");
        return -EINVAL;
    }

    dir = rdma_pci_dma_map(pci_dev, dir_addr, TARGET_PAGE_SIZE);
    if (!dir) {
        rdma_error_report("Failed to map to page directory (ring %s)", name);
        rc = -ENOMEM;
        goto out;
    }
    tbl = rdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        rdma_error_report("Failed to map to page table (ring %s)", name);
        rc = -ENOMEM;
        goto out_free_dir;
    }

    *ring_state = rdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!*ring_state) {
        rdma_error_report("Failed to map to ring state (ring %s)", name);
        rc = -ENOMEM;
        goto out_free_tbl;
    }
    /* RX ring is the second */
    (*ring_state)++;
    rc = pvrdma_ring_init(ring, name, pci_dev,
                          (PvrdmaRingState *)*ring_state,
                          (num_pages - 1) * TARGET_PAGE_SIZE /
                          sizeof(struct pvrdma_cqne),
                          sizeof(struct pvrdma_cqne),
                          (dma_addr_t *)&tbl[1], (dma_addr_t)num_pages - 1);
    if (rc) {
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
    dev->dsr_info.dsr = rdma_pci_dma_map(pci_dev, dev->dsr_info.dma,
                              sizeof(struct pvrdma_device_shared_region));
    if (!dev->dsr_info.dsr) {
        rdma_error_report("Failed to map to DSR");
        rc = -ENOMEM;
        goto out;
    }

    /* Shortcuts */
    dsr_info = &dev->dsr_info;
    dsr = dsr_info->dsr;

    /* Map to command slot */
    dsr_info->req = rdma_pci_dma_map(pci_dev, dsr->cmd_slot_dma,
                                     sizeof(union pvrdma_cmd_req));
    if (!dsr_info->req) {
        rdma_error_report("Failed to map to command slot address");
        rc = -ENOMEM;
        goto out_free_dsr;
    }

    /* Map to response slot */
    dsr_info->rsp = rdma_pci_dma_map(pci_dev, dsr->resp_slot_dma,
                                     sizeof(union pvrdma_cmd_resp));
    if (!dsr_info->rsp) {
        rdma_error_report("Failed to map to response slot address");
        rc = -ENOMEM;
        goto out_free_req;
    }

    /* Map to CQ notification ring */
    rc = init_dev_ring(&dsr_info->cq, &dsr_info->cq_ring_state, "dev_cq",
                       pci_dev, dsr->cq_ring_pages.pdir_dma,
                       dsr->cq_ring_pages.num_pages);
    if (rc) {
        rc = -ENOMEM;
        goto out_free_rsp;
    }

    /* Map to event notification ring */
    rc = init_dev_ring(&dsr_info->async, &dsr_info->async_ring_state,
                       "dev_async", pci_dev, dsr->async_ring_pages.pdir_dma,
                       dsr->async_ring_pages.num_pages);
    if (rc) {
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
        rdma_error_report("Can't initialized DSR");
        return;
    }

    dsr = dev->dsr_info.dsr;
    dsr->caps.fw_ver = PVRDMA_FW_VERSION;
    dsr->caps.mode = PVRDMA_DEVICE_MODE_ROCE;
    dsr->caps.gid_types |= PVRDMA_GID_TYPE_FLAG_ROCE_V1;
    dsr->caps.max_uar = RDMA_BAR2_UAR_SIZE;
    dsr->caps.max_mr_size = dev->dev_attr.max_mr_size;
    dsr->caps.max_qp = dev->dev_attr.max_qp;
    dsr->caps.max_qp_wr = dev->dev_attr.max_qp_wr;
    dsr->caps.max_sge = dev->dev_attr.max_sge;
    dsr->caps.max_cq = dev->dev_attr.max_cq;
    dsr->caps.max_cqe = dev->dev_attr.max_cqe;
    dsr->caps.max_mr = dev->dev_attr.max_mr;
    dsr->caps.max_pd = dev->dev_attr.max_pd;
    dsr->caps.max_ah = dev->dev_attr.max_ah;
    dsr->caps.max_srq = dev->dev_attr.max_srq;
    dsr->caps.max_srq_wr = dev->dev_attr.max_srq_wr;
    dsr->caps.max_srq_sge = dev->dev_attr.max_srq_sge;
    dsr->caps.gid_tbl_len = MAX_GIDS;
    dsr->caps.sys_image_guid = 0;
    dsr->caps.node_guid = dev->node_guid;
    dsr->caps.phys_port_cnt = MAX_PORTS;
    dsr->caps.max_pkeys = MAX_PKEYS;
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

static int init_msix(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    int i;
    int rc;

    rc = msix_init(pdev, RDMA_MAX_INTRS, &dev->msix, RDMA_MSIX_BAR_IDX,
                   RDMA_MSIX_TABLE, &dev->msix, RDMA_MSIX_BAR_IDX,
                   RDMA_MSIX_PBA, 0, NULL);

    if (rc < 0) {
        rdma_error_report("Failed to initialize MSI-X");
        return rc;
    }

    for (i = 0; i < RDMA_MAX_INTRS; i++) {
        rc = msix_vector_use(PCI_DEVICE(dev), i);
        if (rc < 0) {
            rdma_error_report("Fail mark MSI-X vector %d", i);
            uninit_msix(pdev, i);
            return rc;
        }
    }

    return 0;
}

static void pvrdma_fini(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    notifier_remove(&dev->shutdown_notifier);

    pvrdma_qp_ops_fini();

    rdma_backend_stop(&dev->backend_dev);

    rdma_rm_fini(&dev->rdma_dev_res, &dev->backend_dev,
                 dev->backend_eth_device_name);

    rdma_backend_fini(&dev->backend_dev);

    free_dsr(dev);

    if (msix_enabled(pdev)) {
        uninit_msix(pdev, RDMA_MAX_INTRS);
    }

    rdma_info_report("Device %s %x.%x is down", pdev->name,
                     PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
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
}

static int unquiesce_device(PVRDMADev *dev)
{
    return 0;
}

static void reset_device(PVRDMADev *dev)
{
    pvrdma_stop(dev);
}

static uint64_t pvrdma_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    PVRDMADev *dev = opaque;
    uint32_t val;

    dev->stats.regs_reads++;

    if (get_reg_val(dev, addr, &val)) {
        rdma_error_report("Failed to read REG value from address 0x%x",
                          (uint32_t)addr);
        return -EINVAL;
    }

    trace_pvrdma_regs_read(addr, val);

    return val;
}

static void pvrdma_regs_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    PVRDMADev *dev = opaque;

    dev->stats.regs_writes++;

    if (set_reg_val(dev, addr, val)) {
        rdma_error_report("Failed to set REG value, addr=0x%"PRIx64 ", val=0x%"PRIx64,
                          addr, val);
        return;
    }

    switch (addr) {
    case PVRDMA_REG_DSRLOW:
        trace_pvrdma_regs_write(addr, val, "DSRLOW", "");
        dev->dsr_info.dma = val;
        break;
    case PVRDMA_REG_DSRHIGH:
        trace_pvrdma_regs_write(addr, val, "DSRHIGH", "");
        dev->dsr_info.dma |= val << 32;
        load_dsr(dev);
        init_dsr_dev_caps(dev);
        break;
    case PVRDMA_REG_CTL:
        switch (val) {
        case PVRDMA_DEVICE_CTL_ACTIVATE:
            trace_pvrdma_regs_write(addr, val, "CTL", "ACTIVATE");
            activate_device(dev);
            break;
        case PVRDMA_DEVICE_CTL_UNQUIESCE:
            trace_pvrdma_regs_write(addr, val, "CTL", "UNQUIESCE");
            unquiesce_device(dev);
            break;
        case PVRDMA_DEVICE_CTL_RESET:
            trace_pvrdma_regs_write(addr, val, "CTL", "URESET");
            reset_device(dev);
            break;
        }
        break;
    case PVRDMA_REG_IMR:
        trace_pvrdma_regs_write(addr, val, "INTR_MASK", "");
        dev->interrupt_mask = val;
        break;
    case PVRDMA_REG_REQUEST:
        if (val == 0) {
            trace_pvrdma_regs_write(addr, val, "REQUEST", "");
            pvrdma_exec_cmd(dev);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps regs_ops = {
    .read = pvrdma_regs_read,
    .write = pvrdma_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = sizeof(uint32_t),
        .max_access_size = sizeof(uint32_t),
    },
};

static uint64_t pvrdma_uar_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffff;
}

static void pvrdma_uar_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    PVRDMADev *dev = opaque;

    dev->stats.uar_writes++;

    switch (addr & 0xFFF) { /* Mask with 0xFFF as each UC gets page */
    case PVRDMA_UAR_QP_OFFSET:
        if (val & PVRDMA_UAR_QP_SEND) {
            trace_pvrdma_uar_write(addr, val, "QP", "SEND",
                                   val & PVRDMA_UAR_HANDLE_MASK, 0);
            pvrdma_qp_send(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        if (val & PVRDMA_UAR_QP_RECV) {
            trace_pvrdma_uar_write(addr, val, "QP", "RECV",
                                   val & PVRDMA_UAR_HANDLE_MASK, 0);
            pvrdma_qp_recv(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        break;
    case PVRDMA_UAR_CQ_OFFSET:
        if (val & PVRDMA_UAR_CQ_ARM) {
            trace_pvrdma_uar_write(addr, val, "CQ", "ARM",
                                   val & PVRDMA_UAR_HANDLE_MASK,
                                   !!(val & PVRDMA_UAR_CQ_ARM_SOL));
            rdma_rm_req_notify_cq(&dev->rdma_dev_res,
                                  val & PVRDMA_UAR_HANDLE_MASK,
                                  !!(val & PVRDMA_UAR_CQ_ARM_SOL));
        }
        if (val & PVRDMA_UAR_CQ_ARM_SOL) {
            trace_pvrdma_uar_write(addr, val, "CQ", "ARMSOL - not supported", 0,
                                   0);
        }
        if (val & PVRDMA_UAR_CQ_POLL) {
            trace_pvrdma_uar_write(addr, val, "CQ", "POLL",
                                   val & PVRDMA_UAR_HANDLE_MASK, 0);
            pvrdma_cq_poll(&dev->rdma_dev_res, val & PVRDMA_UAR_HANDLE_MASK);
        }
        break;
    case PVRDMA_UAR_SRQ_OFFSET:
        if (val & PVRDMA_UAR_SRQ_RECV) {
            trace_pvrdma_uar_write(addr, val, "QP", "SRQ",
                                   val & PVRDMA_UAR_HANDLE_MASK, 0);
            pvrdma_srq_recv(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        break;
    default:
        rdma_error_report("Unsupported command, addr=0x%"PRIx64", val=0x%"PRIx64,
                          addr, val);
        break;
    }
}

static const MemoryRegionOps uar_ops = {
    .read = pvrdma_uar_read,
    .write = pvrdma_uar_write,
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
                              (wr_sz + sizeof(struct pvrdma_sge) *
                              dev->dev_attr.max_sge) - TARGET_PAGE_SIZE;
                              /* First page is ring state  ^^^^ */

    dev->dev_attr.max_cqe = pg_tbl_bytes / sizeof(struct pvrdma_cqe) -
                            TARGET_PAGE_SIZE; /* First page is ring state */

    dev->dev_attr.max_srq_wr = pg_tbl_bytes /
                                ((sizeof(struct pvrdma_rq_wqe_hdr) +
                                sizeof(struct pvrdma_sge)) *
                                dev->dev_attr.max_sge) - TARGET_PAGE_SIZE;
}

static int pvrdma_check_ram_shared(Object *obj, void *opaque)
{
    bool *shared = opaque;

    if (object_dynamic_cast(obj, "memory-backend-ram")) {
        *shared = object_property_get_bool(obj, "share", NULL);
    }

    return 0;
}

static void pvrdma_shutdown_notifier(Notifier *n, void *opaque)
{
    PVRDMADev *dev = container_of(n, PVRDMADev, shutdown_notifier);
    PCIDevice *pci_dev = PCI_DEVICE(dev);

    pvrdma_fini(pci_dev);
}

static void pvrdma_realize(PCIDevice *pdev, Error **errp)
{
    int rc = 0;
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    Object *memdev_root;
    bool ram_shared = false;
    PCIDevice *func0;

    rdma_info_report("Initializing device %s %x.%x", pdev->name,
                     PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

    if (TARGET_PAGE_SIZE != qemu_real_host_page_size) {
        error_setg(errp, "Target page size must be the same as host page size");
        return;
    }

    func0 = pci_get_function_0(pdev);
    /* Break if not vmxnet3 device in slot 0 */
    if (strcmp(object_get_typename(OBJECT(func0)), TYPE_VMXNET3)) {
        error_setg(errp, "Device on %x.0 must be %s", PCI_SLOT(pdev->devfn),
                   TYPE_VMXNET3);
        return;
    }
    dev->func0 = VMXNET3(func0);

    addrconf_addr_eui48((unsigned char *)&dev->node_guid,
                        (const char *)&dev->func0->conf.macaddr.a);

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

    rc = init_msix(pdev);
    if (rc) {
        goto out;
    }

    rc = rdma_backend_init(&dev->backend_dev, pdev, &dev->rdma_dev_res,
                           dev->backend_device_name, dev->backend_port_num,
                           &dev->dev_attr, &dev->mad_chr);
    if (rc) {
        goto out;
    }

    init_dev_caps(dev);

    rc = rdma_rm_init(&dev->rdma_dev_res, &dev->dev_attr);
    if (rc) {
        goto out;
    }

    rc = pvrdma_qp_ops_init();
    if (rc) {
        goto out;
    }

    memset(&dev->stats, 0, sizeof(dev->stats));

    dev->shutdown_notifier.notify = pvrdma_shutdown_notifier;
    qemu_register_shutdown_notifier(&dev->shutdown_notifier);

#ifdef LEGACY_RDMA_REG_MR
    rdma_info_report("Using legacy reg_mr");
#else
    rdma_info_report("Using iova reg_mr");
#endif

out:
    if (rc) {
        pvrdma_fini(pdev);
        error_append_hint(errp, "Device failed to load\n");
    }
}

static void pvrdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    RdmaProviderClass *ir = RDMA_PROVIDER_CLASS(klass);

    k->realize = pvrdma_realize;
    k->vendor_id = PCI_VENDOR_ID_VMWARE;
    k->device_id = PCI_DEVICE_ID_VMWARE_PVRDMA;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_OTHER;

    dc->desc = "RDMA Device";
    device_class_set_props(dc, pvrdma_dev_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);

    ir->format_statistics = pvrdma_format_statistics;
}

static const TypeInfo pvrdma_info = {
    .name = PVRDMA_HW_NAME,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PVRDMADev),
    .class_init = pvrdma_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { INTERFACE_RDMA_PROVIDER },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&pvrdma_info);
}

type_init(register_types)
