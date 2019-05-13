/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "kvm_ppc.h"

#include <sys/ioctl.h>

/*
 * Helpers for CPU hotplug
 *
 * TODO: make a common KVMEnabledCPU layer for XICS and XIVE
 */
typedef struct KVMEnabledCPU {
    unsigned long vcpu_id;
    QLIST_ENTRY(KVMEnabledCPU) node;
} KVMEnabledCPU;

static QLIST_HEAD(, KVMEnabledCPU)
    kvm_enabled_cpus = QLIST_HEAD_INITIALIZER(&kvm_enabled_cpus);

static bool kvm_cpu_is_enabled(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    QLIST_FOREACH(enabled_cpu, &kvm_enabled_cpus, node) {
        if (enabled_cpu->vcpu_id == vcpu_id) {
            return true;
        }
    }
    return false;
}

static void kvm_cpu_enable(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    enabled_cpu = g_malloc(sizeof(*enabled_cpu));
    enabled_cpu->vcpu_id = vcpu_id;
    QLIST_INSERT_HEAD(&kvm_enabled_cpus, enabled_cpu, node);
}

/*
 * XIVE Thread Interrupt Management context (KVM)
 */

void kvmppc_xive_cpu_connect(XiveTCTX *tctx, Error **errp)
{
    SpaprXive *xive = SPAPR_MACHINE(qdev_get_machine())->xive;
    unsigned long vcpu_id;
    int ret;

    /* Check if CPU was hot unplugged and replugged. */
    if (kvm_cpu_is_enabled(tctx->cs)) {
        return;
    }

    vcpu_id = kvm_arch_vcpu_id(tctx->cs);

    ret = kvm_vcpu_enable_cap(tctx->cs, KVM_CAP_PPC_IRQ_XIVE, 0, xive->fd,
                              vcpu_id, 0);
    if (ret < 0) {
        error_setg(errp, "XIVE: unable to connect CPU%ld to KVM device: %s",
                   vcpu_id, strerror(errno));
        return;
    }

    kvm_cpu_enable(tctx->cs);
}

/*
 * XIVE Interrupt Source (KVM)
 */

/*
 * At reset, the interrupt sources are simply created and MASKED. We
 * only need to inform the KVM XIVE device about their type: LSI or
 * MSI.
 */
void kvmppc_xive_source_reset_one(XiveSource *xsrc, int srcno, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    uint64_t state = 0;

    if (xive_source_irq_is_lsi(xsrc, srcno)) {
        state |= KVM_XIVE_LEVEL_SENSITIVE;
        if (xsrc->status[srcno] & XIVE_STATUS_ASSERTED) {
            state |= KVM_XIVE_LEVEL_ASSERTED;
        }
    }

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE, srcno, &state,
                      true, errp);
}

void kvmppc_xive_source_reset(XiveSource *xsrc, Error **errp)
{
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        Error *local_err = NULL;

        kvmppc_xive_source_reset_one(xsrc, i, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

void kvmppc_xive_source_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = opaque;
    struct kvm_irq_level args;
    int rc;

    args.irq = srcno;
    if (!xive_source_irq_is_lsi(xsrc, srcno)) {
        if (!val) {
            return;
        }
        args.level = KVM_INTERRUPT_SET;
    } else {
        if (val) {
            xsrc->status[srcno] |= XIVE_STATUS_ASSERTED;
            args.level = KVM_INTERRUPT_SET_LEVEL;
        } else {
            xsrc->status[srcno] &= ~XIVE_STATUS_ASSERTED;
            args.level = KVM_INTERRUPT_UNSET;
        }
    }
    rc = kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &args);
    if (rc < 0) {
        error_report("XIVE: kvm_irq_line() failed : %s", strerror(errno));
    }
}

/*
 * sPAPR XIVE interrupt controller (KVM)
 */

static void *kvmppc_xive_mmap(SpaprXive *xive, int pgoff, size_t len,
                              Error **errp)
{
    void *addr;
    uint32_t page_shift = 16; /* TODO: fix page_shift */

    addr = mmap(NULL, len, PROT_WRITE | PROT_READ, MAP_SHARED, xive->fd,
                pgoff << page_shift);
    if (addr == MAP_FAILED) {
        error_setg_errno(errp, errno, "XIVE: unable to set memory mapping");
        return NULL;
    }

    return addr;
}

/*
 * All the XIVE memory regions are now backed by mappings from the KVM
 * XIVE device.
 */
void kvmppc_xive_connect(SpaprXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    XiveENDSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;
    size_t esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;
    size_t tima_len = 4ull << TM_SHIFT;

    if (!kvmppc_has_cap_xive()) {
        error_setg(errp, "IRQ_XIVE capability must be present for KVM");
        return;
    }

    /* First, create the KVM XIVE device */
    xive->fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_XIVE, false);
    if (xive->fd < 0) {
        error_setg_errno(errp, -xive->fd, "XIVE: error creating KVM device");
        return;
    }

    /*
     * 1. Source ESB pages - KVM mapping
     */
    xsrc->esb_mmap = kvmppc_xive_mmap(xive, KVM_XIVE_ESB_PAGE_OFFSET, esb_len,
                                      &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_ram_device_ptr(&xsrc->esb_mmio, OBJECT(xsrc),
                                      "xive.esb", esb_len, xsrc->esb_mmap);
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &xsrc->esb_mmio);

    /*
     * 2. END ESB pages (No KVM support yet)
     */
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &end_xsrc->esb_mmio);

    /*
     * 3. TIMA pages - KVM mapping
     */
    xive->tm_mmap = kvmppc_xive_mmap(xive, KVM_XIVE_TIMA_PAGE_OFFSET, tima_len,
                                     &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_init_ram_device_ptr(&xive->tm_mmio, OBJECT(xive),
                                      "xive.tima", tima_len, xive->tm_mmap);
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &xive->tm_mmio);

    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;

    /* Map all regions */
    spapr_xive_map_mmio(xive);
}
