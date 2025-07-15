/*
 * vfio based device assignment support
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES /* CONFIG_IOMMUFD */
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_bridge.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/vfio/vfio-cpr.h"
#include "migration/vmstate.h"
#include "migration/cpr.h"
#include "qobject/qdict.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "system/kvm.h"
#include "system/runstate.h"
#include "pci.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/blocker.h"
#include "migration/qemu-file.h"
#include "system/iommufd.h"
#include "vfio-migration-internal.h"
#include "vfio-helpers.h"

#define TYPE_VFIO_PCI_NOHOTPLUG "vfio-pci-nohotplug"

/* Protected by BQL */
static KVMRouteChange vfio_route_change;

static void vfio_disable_interrupts(VFIOPCIDevice *vdev);
static void vfio_mmap_set_enabled(VFIOPCIDevice *vdev, bool enabled);
static void vfio_msi_disable_common(VFIOPCIDevice *vdev);

/* Create new or reuse existing eventfd */
static bool vfio_notifier_init(VFIOPCIDevice *vdev, EventNotifier *e,
                               const char *name, int nr, Error **errp)
{
    int fd, ret;

    fd = vfio_cpr_load_vector_fd(vdev, name, nr);
    if (fd >= 0) {
        event_notifier_init_fd(e, fd);
        return true;
    }

    ret = event_notifier_init(e, 0);
    if (ret) {
        error_setg_errno(errp, -ret, "vfio_notifier_init %s failed", name);
        return false;
    }

    fd = event_notifier_get_fd(e);
    vfio_cpr_save_vector_fd(vdev, name, nr, fd);
    return true;
}

static void vfio_notifier_cleanup(VFIOPCIDevice *vdev, EventNotifier *e,
                                  const char *name, int nr)
{
    vfio_cpr_delete_vector_fd(vdev, name, nr);
    event_notifier_cleanup(e);
}

/*
 * Disabling BAR mmaping can be slow, but toggling it around INTx can
 * also be a huge overhead.  We try to get the best of both worlds by
 * waiting until an interrupt to disable mmaps (subsequent transitions
 * to the same state are effectively no overhead).  If the interrupt has
 * been serviced and the time gap is long enough, we re-enable mmaps for
 * performance.  This works well for things like graphics cards, which
 * may not use their interrupt at all and are penalized to an unusable
 * level by read/write BAR traps.  Other devices, like NICs, have more
 * regular interrupts and see much better latency by staying in non-mmap
 * mode.  We therefore set the default mmap_timeout such that a ping
 * is just enough to keep the mmap disabled.  Users can experiment with
 * other options with the x-intx-mmap-timeout-ms parameter (a value of
 * zero disables the timer).
 */
static void vfio_intx_mmap_enable(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    if (vdev->intx.pending) {
        timer_mod(vdev->intx.mmap_timer,
                       qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vdev->intx.mmap_timeout);
        return;
    }

    vfio_mmap_set_enabled(vdev, true);
}

static void vfio_intx_interrupt(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    if (!event_notifier_test_and_clear(&vdev->intx.interrupt)) {
        return;
    }

    trace_vfio_intx_interrupt(vdev->vbasedev.name, 'A' + vdev->intx.pin);

    vdev->intx.pending = true;
    pci_irq_assert(&vdev->pdev);
    vfio_mmap_set_enabled(vdev, false);
    if (vdev->intx.mmap_timeout) {
        timer_mod(vdev->intx.mmap_timer,
                       qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vdev->intx.mmap_timeout);
    }
}

void vfio_pci_intx_eoi(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    if (!vdev->intx.pending) {
        return;
    }

    trace_vfio_pci_intx_eoi(vbasedev->name);

    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);
    vfio_device_irq_unmask(vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
}

static bool vfio_intx_enable_kvm(VFIOPCIDevice *vdev, Error **errp)
{
#ifdef CONFIG_KVM
    int irq_fd = event_notifier_get_fd(&vdev->intx.interrupt);

    if (vdev->no_kvm_intx || !kvm_irqfds_enabled() ||
        vdev->intx.route.mode != PCI_INTX_ENABLED ||
        !kvm_resamplefds_enabled()) {
        return true;
    }

    /* Get to a known interrupt state */
    qemu_set_fd_handler(irq_fd, NULL, NULL, vdev);
    vfio_device_irq_mask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);

    /* Get an eventfd for resample/unmask */
    if (!vfio_notifier_init(vdev, &vdev->intx.unmask, "intx-unmask", 0, errp)) {
        goto fail;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &vdev->intx.interrupt,
                                           &vdev->intx.unmask,
                                           vdev->intx.route.irq)) {
        error_setg_errno(errp, errno, "failed to setup resample irqfd");
        goto fail_irqfd;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_UNMASK,
                                       event_notifier_get_fd(&vdev->intx.unmask),
                                       errp)) {
        goto fail_vfio;
    }

    /* Let'em rip */
    vfio_device_irq_unmask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);

    vdev->intx.kvm_accel = true;

    trace_vfio_intx_enable_kvm(vdev->vbasedev.name);

    return true;

fail_vfio:
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vdev->intx.interrupt,
                                          vdev->intx.route.irq);
fail_irqfd:
    vfio_notifier_cleanup(vdev, &vdev->intx.unmask, "intx-unmask", 0);
fail:
    qemu_set_fd_handler(irq_fd, vfio_intx_interrupt, NULL, vdev);
    vfio_device_irq_unmask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    return false;
#else
    return true;
#endif
}

static bool vfio_cpr_intx_enable_kvm(VFIOPCIDevice *vdev, Error **errp)
{
#ifdef CONFIG_KVM
    if (vdev->no_kvm_intx || !kvm_irqfds_enabled() ||
        vdev->intx.route.mode != PCI_INTX_ENABLED ||
        !kvm_resamplefds_enabled()) {
        return true;
    }

    if (!vfio_notifier_init(vdev, &vdev->intx.unmask, "intx-unmask", 0, errp)) {
        return false;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &vdev->intx.interrupt,
                                           &vdev->intx.unmask,
                                           vdev->intx.route.irq)) {
        error_setg_errno(errp, errno, "failed to setup resample irqfd");
        vfio_notifier_cleanup(vdev, &vdev->intx.unmask, "intx-unmask", 0);
        return false;
    }

    vdev->intx.kvm_accel = true;
    trace_vfio_intx_enable_kvm(vdev->vbasedev.name);
    return true;
#else
    return true;
#endif
}

static void vfio_intx_disable_kvm(VFIOPCIDevice *vdev)
{
#ifdef CONFIG_KVM
    if (!vdev->intx.kvm_accel) {
        return;
    }

    /*
     * Get to a known state, hardware masked, QEMU ready to accept new
     * interrupts, QEMU IRQ de-asserted.
     */
    vfio_device_irq_mask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);

    /* Tell KVM to stop listening for an INTx irqfd */
    if (kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vdev->intx.interrupt,
                                              vdev->intx.route.irq)) {
        error_report("vfio: Error: Failed to disable INTx irqfd: %m");
    }

    /* We only need to close the eventfd for VFIO to cleanup the kernel side */
    vfio_notifier_cleanup(vdev, &vdev->intx.unmask, "intx-unmask", 0);

    /* QEMU starts listening for interrupt events. */
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->intx.interrupt),
                        vfio_intx_interrupt, NULL, vdev);

    vdev->intx.kvm_accel = false;

    /* If we've missed an event, let it re-fire through QEMU */
    vfio_device_irq_unmask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);

    trace_vfio_intx_disable_kvm(vdev->vbasedev.name);
#endif
}

static void vfio_intx_update(VFIOPCIDevice *vdev, PCIINTxRoute *route)
{
    Error *err = NULL;

    trace_vfio_intx_update(vdev->vbasedev.name,
                           vdev->intx.route.irq, route->irq);

    vfio_intx_disable_kvm(vdev);

    vdev->intx.route = *route;

    if (route->mode != PCI_INTX_ENABLED) {
        return;
    }

    if (!vfio_intx_enable_kvm(vdev, &err)) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    /* Re-enable the interrupt in cased we missed an EOI */
    vfio_pci_intx_eoi(&vdev->vbasedev);
}

static void vfio_intx_routing_notifier(PCIDevice *pdev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    PCIINTxRoute route;

    if (vdev->interrupt != VFIO_INT_INTx) {
        return;
    }

    route = pci_device_route_intx_to_irq(&vdev->pdev, vdev->intx.pin);

    if (pci_intx_route_changed(&vdev->intx.route, &route)) {
        vfio_intx_update(vdev, &route);
    }
}

static void vfio_irqchip_change(Notifier *notify, void *data)
{
    VFIOPCIDevice *vdev = container_of(notify, VFIOPCIDevice,
                                       irqchip_change_notifier);

    vfio_intx_update(vdev, &vdev->intx.route);
}

static bool vfio_intx_enable(VFIOPCIDevice *vdev, Error **errp)
{
    uint8_t pin = vfio_pci_read_config(&vdev->pdev, PCI_INTERRUPT_PIN, 1);
    Error *err = NULL;
    int32_t fd;


    if (!pin) {
        return true;
    }

    /*
     * Do not alter interrupt state during vfio_realize and cpr load.
     * The incoming state is cleared thereafter.
     */
    if (!cpr_is_incoming()) {
        vfio_disable_interrupts(vdev);
    }

    vdev->intx.pin = pin - 1; /* Pin A (1) -> irq[0] */
    pci_config_set_interrupt_pin(vdev->pdev.config, pin);

#ifdef CONFIG_KVM
    /*
     * Only conditional to avoid generating error messages on platforms
     * where we won't actually use the result anyway.
     */
    if (kvm_irqfds_enabled() && kvm_resamplefds_enabled()) {
        vdev->intx.route = pci_device_route_intx_to_irq(&vdev->pdev,
                                                        vdev->intx.pin);
    }
#endif

    if (!vfio_notifier_init(vdev, &vdev->intx.interrupt, "intx-interrupt", 0,
                            errp)) {
        return false;
    }
    fd = event_notifier_get_fd(&vdev->intx.interrupt);
    qemu_set_fd_handler(fd, vfio_intx_interrupt, NULL, vdev);


    if (cpr_is_incoming()) {
        if (!vfio_cpr_intx_enable_kvm(vdev, &err)) {
            warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        }
        goto skip_signaling;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX, 0,
                                VFIO_IRQ_SET_ACTION_TRIGGER, fd, errp)) {
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        vfio_notifier_cleanup(vdev, &vdev->intx.interrupt, "intx-interrupt", 0);
        return false;
    }

    if (!vfio_intx_enable_kvm(vdev, &err)) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

skip_signaling:
    vdev->interrupt = VFIO_INT_INTx;

    trace_vfio_intx_enable(vdev->vbasedev.name);
    return true;
}

static void vfio_intx_disable(VFIOPCIDevice *vdev)
{
    int fd;

    timer_del(vdev->intx.mmap_timer);
    vfio_intx_disable_kvm(vdev);
    vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);
    vfio_mmap_set_enabled(vdev, true);

    fd = event_notifier_get_fd(&vdev->intx.interrupt);
    qemu_set_fd_handler(fd, NULL, NULL, vdev);
    vfio_notifier_cleanup(vdev, &vdev->intx.interrupt, "intx-interrupt", 0);

    vdev->interrupt = VFIO_INT_NONE;

    trace_vfio_intx_disable(vdev->vbasedev.name);
}

bool vfio_pci_intx_enable(VFIOPCIDevice *vdev, Error **errp)
{
    return vfio_intx_enable(vdev, errp);
}

/*
 * MSI/X
 */
static void vfio_msi_interrupt(void *opaque)
{
    VFIOMSIVector *vector = opaque;
    VFIOPCIDevice *vdev = vector->vdev;
    MSIMessage (*get_msg)(PCIDevice *dev, unsigned vector);
    void (*notify)(PCIDevice *dev, unsigned vector);
    MSIMessage msg;
    int nr = vector - vdev->msi_vectors;

    if (!event_notifier_test_and_clear(&vector->interrupt)) {
        return;
    }

    if (vdev->interrupt == VFIO_INT_MSIX) {
        get_msg = msix_get_message;
        notify = msix_notify;

        /* A masked vector firing needs to use the PBA, enable it */
        if (msix_is_masked(&vdev->pdev, nr)) {
            set_bit(nr, vdev->msix->pending);
            memory_region_set_enabled(&vdev->pdev.msix_pba_mmio, true);
            trace_vfio_msix_pba_enable(vdev->vbasedev.name);
        }
    } else if (vdev->interrupt == VFIO_INT_MSI) {
        get_msg = msi_get_message;
        notify = msi_notify;
    } else {
        abort();
    }

    msg = get_msg(&vdev->pdev, nr);
    trace_vfio_msi_interrupt(vdev->vbasedev.name, nr, msg.address, msg.data);
    notify(&vdev->pdev, nr);
}

void vfio_pci_msi_set_handler(VFIOPCIDevice *vdev, int nr)
{
    VFIOMSIVector *vector = &vdev->msi_vectors[nr];
    int fd = event_notifier_get_fd(&vector->interrupt);

    qemu_set_fd_handler(fd, vfio_msi_interrupt, NULL, vector);
}

/*
 * Get MSI-X enabled, but no vector enabled, by setting vector 0 with an invalid
 * fd to kernel.
 */
static int vfio_enable_msix_no_vec(VFIOPCIDevice *vdev)
{
    g_autofree struct vfio_irq_set *irq_set = NULL;
    int argsz;
    int32_t *fd;

    argsz = sizeof(*irq_set) + sizeof(*fd);

    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
                     VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    fd = (int32_t *)&irq_set->data;
    *fd = -1;

    return vdev->vbasedev.io_ops->set_irqs(&vdev->vbasedev, irq_set);
}

static int vfio_enable_vectors(VFIOPCIDevice *vdev, bool msix)
{
    struct vfio_irq_set *irq_set;
    int ret = 0, i, argsz;
    int32_t *fds;

    /*
     * If dynamic MSI-X allocation is supported, the vectors to be allocated
     * and enabled can be scattered. Before kernel enabling MSI-X, setting
     * nr_vectors causes all these vectors to be allocated on host.
     *
     * To keep allocation as needed, use vector 0 with an invalid fd to get
     * MSI-X enabled first, then set vectors with a potentially sparse set of
     * eventfds to enable interrupts only when enabled in guest.
     */
    if (msix && !vdev->msix->noresize) {
        ret = vfio_enable_msix_no_vec(vdev);

        if (ret) {
            return ret;
        }
    }

    argsz = sizeof(*irq_set) + (vdev->nr_vectors * sizeof(*fds));

    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = msix ? VFIO_PCI_MSIX_IRQ_INDEX : VFIO_PCI_MSI_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = vdev->nr_vectors;
    fds = (int32_t *)&irq_set->data;

    for (i = 0; i < vdev->nr_vectors; i++) {
        int fd = -1;

        /*
         * MSI vs MSI-X - The guest has direct access to MSI mask and pending
         * bits, therefore we always use the KVM signaling path when setup.
         * MSI-X mask and pending bits are emulated, so we want to use the
         * KVM signaling path only when configured and unmasked.
         */
        if (vdev->msi_vectors[i].use) {
            if (vdev->msi_vectors[i].virq < 0 ||
                (msix && msix_is_masked(&vdev->pdev, i))) {
                fd = event_notifier_get_fd(&vdev->msi_vectors[i].interrupt);
            } else {
                fd = event_notifier_get_fd(&vdev->msi_vectors[i].kvm_interrupt);
            }
        }

        fds[i] = fd;
    }

    ret = vdev->vbasedev.io_ops->set_irqs(&vdev->vbasedev, irq_set);

    g_free(irq_set);

    return ret;
}

void vfio_pci_add_kvm_msi_virq(VFIOPCIDevice *vdev, VFIOMSIVector *vector,
                               int vector_n, bool msix)
{
    if ((msix && vdev->no_kvm_msix) || (!msix && vdev->no_kvm_msi)) {
        return;
    }

    vector->virq = kvm_irqchip_add_msi_route(&vfio_route_change,
                                             vector_n, &vdev->pdev);
}

static void vfio_connect_kvm_msi_virq(VFIOMSIVector *vector, int nr)
{
    const char *name = "kvm_interrupt";

    if (vector->virq < 0) {
        return;
    }

    if (!vfio_notifier_init(vector->vdev, &vector->kvm_interrupt, name, nr,
                            NULL)) {
        goto fail_notifier;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, &vector->kvm_interrupt,
                                           NULL, vector->virq) < 0) {
        goto fail_kvm;
    }

    return;

fail_kvm:
    vfio_notifier_cleanup(vector->vdev, &vector->kvm_interrupt, name, nr);
fail_notifier:
    kvm_irqchip_release_virq(kvm_state, vector->virq);
    vector->virq = -1;
}

static void vfio_remove_kvm_msi_virq(VFIOPCIDevice *vdev, VFIOMSIVector *vector,
                                     int nr)
{
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vector->kvm_interrupt,
                                          vector->virq);
    kvm_irqchip_release_virq(kvm_state, vector->virq);
    vector->virq = -1;
    vfio_notifier_cleanup(vdev, &vector->kvm_interrupt, "kvm_interrupt", nr);
}

static void vfio_update_kvm_msi_virq(VFIOMSIVector *vector, MSIMessage msg,
                                     PCIDevice *pdev)
{
    kvm_irqchip_update_msi_route(kvm_state, vector->virq, msg, pdev);
    kvm_irqchip_commit_routes(kvm_state);
}

static void set_irq_signalling(VFIODevice *vbasedev, VFIOMSIVector *vector,
                               unsigned int nr)
{
    Error *err = NULL;
    int32_t fd;

    if (vector->virq >= 0) {
        fd = event_notifier_get_fd(&vector->kvm_interrupt);
    } else {
        fd = event_notifier_get_fd(&vector->interrupt);
    }

    if (!vfio_device_irq_set_signaling(vbasedev, VFIO_PCI_MSIX_IRQ_INDEX, nr,
                                       VFIO_IRQ_SET_ACTION_TRIGGER,
                                       fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vbasedev->name);
    }
}

void vfio_pci_vector_init(VFIOPCIDevice *vdev, int nr)
{
    VFIOMSIVector *vector = &vdev->msi_vectors[nr];
    PCIDevice *pdev = &vdev->pdev;
    Error *local_err = NULL;

    vector->vdev = vdev;
    vector->virq = -1;
    if (!vfio_notifier_init(vdev, &vector->interrupt, "interrupt", nr,
                            &local_err)) {
        error_report_err(local_err);
    }
    vector->use = true;
    if (vdev->interrupt == VFIO_INT_MSIX) {
        msix_vector_use(pdev, nr);
    }
}

static int vfio_msix_vector_do_use(PCIDevice *pdev, unsigned int nr,
                                   MSIMessage *msg, IOHandler *handler)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIOMSIVector *vector;
    int ret;
    bool resizing = !!(vdev->nr_vectors < nr + 1);

    trace_vfio_msix_vector_do_use(vdev->vbasedev.name, nr);

    vector = &vdev->msi_vectors[nr];

    if (!vector->use) {
        vfio_pci_vector_init(vdev, nr);
    }

    qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                        handler, NULL, vector);

    /*
     * Attempt to enable route through KVM irqchip,
     * default to userspace handling if unavailable.
     */
    if (vector->virq >= 0) {
        if (!msg) {
            vfio_remove_kvm_msi_virq(vdev, vector, nr);
        } else {
            vfio_update_kvm_msi_virq(vector, *msg, pdev);
        }
    } else {
        if (msg) {
            if (vdev->defer_kvm_irq_routing) {
                vfio_pci_add_kvm_msi_virq(vdev, vector, nr, true);
            } else {
                vfio_route_change = kvm_irqchip_begin_route_changes(kvm_state);
                vfio_pci_add_kvm_msi_virq(vdev, vector, nr, true);
                kvm_irqchip_commit_route_changes(&vfio_route_change);
                vfio_connect_kvm_msi_virq(vector, nr);
            }
        }
    }

    /*
     * When dynamic allocation is not supported, we don't want to have the
     * host allocate all possible MSI vectors for a device if they're not
     * in use, so we shutdown and incrementally increase them as needed.
     * nr_vectors represents the total number of vectors allocated.
     *
     * When dynamic allocation is supported, let the host only allocate
     * and enable a vector when it is in use in guest. nr_vectors represents
     * the upper bound of vectors being enabled (but not all of the ranges
     * is allocated or enabled).
     */
    if (resizing) {
        vdev->nr_vectors = nr + 1;
    }

    if (!vdev->defer_kvm_irq_routing) {
        if (vdev->msix->noresize && resizing) {
            vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX);
            ret = vfio_enable_vectors(vdev, true);
            if (ret) {
                error_report("vfio: failed to enable vectors, %s",
                             strerror(-ret));
            }
        } else {
            set_irq_signalling(&vdev->vbasedev, vector, nr);
        }
    }

    /* Disable PBA emulation when nothing more is pending. */
    clear_bit(nr, vdev->msix->pending);
    if (find_first_bit(vdev->msix->pending,
                       vdev->nr_vectors) == vdev->nr_vectors) {
        memory_region_set_enabled(&vdev->pdev.msix_pba_mmio, false);
        trace_vfio_msix_pba_disable(vdev->vbasedev.name);
    }

    return 0;
}

static int vfio_msix_vector_use(PCIDevice *pdev,
                                unsigned int nr, MSIMessage msg)
{
    /*
     * Ignore the callback from msix_set_vector_notifiers during resume.
     * The necessary subset of these actions is called from
     * vfio_cpr_claim_vectors during post load.
     */
    if (cpr_is_incoming()) {
        return 0;
    }

    return vfio_msix_vector_do_use(pdev, nr, &msg, vfio_msi_interrupt);
}

static void vfio_msix_vector_release(PCIDevice *pdev, unsigned int nr)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIOMSIVector *vector = &vdev->msi_vectors[nr];

    trace_vfio_msix_vector_release(vdev->vbasedev.name, nr);

    /*
     * There are still old guests that mask and unmask vectors on every
     * interrupt.  If we're using QEMU bypass with a KVM irqfd, leave all of
     * the KVM setup in place, simply switch VFIO to use the non-bypass
     * eventfd.  We'll then fire the interrupt through QEMU and the MSI-X
     * core will mask the interrupt and set pending bits, allowing it to
     * be re-asserted on unmask.  Nothing to do if already using QEMU mode.
     */
    if (vector->virq >= 0) {
        int32_t fd = event_notifier_get_fd(&vector->interrupt);
        Error *err = NULL;

        if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX,
                                    nr, VFIO_IRQ_SET_ACTION_TRIGGER, fd,
                                    &err)) {
            error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        }
    }
}

void vfio_pci_msix_set_notifiers(VFIOPCIDevice *vdev)
{
    msix_set_vector_notifiers(&vdev->pdev, vfio_msix_vector_use,
                              vfio_msix_vector_release, NULL);
}

void vfio_pci_prepare_kvm_msi_virq_batch(VFIOPCIDevice *vdev)
{
    assert(!vdev->defer_kvm_irq_routing);
    vdev->defer_kvm_irq_routing = true;
    vfio_route_change = kvm_irqchip_begin_route_changes(kvm_state);
}

void vfio_pci_commit_kvm_msi_virq_batch(VFIOPCIDevice *vdev)
{
    int i;

    assert(vdev->defer_kvm_irq_routing);
    vdev->defer_kvm_irq_routing = false;

    kvm_irqchip_commit_route_changes(&vfio_route_change);

    for (i = 0; i < vdev->nr_vectors; i++) {
        vfio_connect_kvm_msi_virq(&vdev->msi_vectors[i], i);
    }
}

static void vfio_msix_enable(VFIOPCIDevice *vdev)
{
    int ret;

    vfio_disable_interrupts(vdev);

    vdev->msi_vectors = g_new0(VFIOMSIVector, vdev->msix->entries);

    vdev->interrupt = VFIO_INT_MSIX;

    /*
     * Setting vector notifiers triggers synchronous vector-use
     * callbacks for each active vector.  Deferring to commit the KVM
     * routes once rather than per vector provides a substantial
     * performance improvement.
     */
    vfio_pci_prepare_kvm_msi_virq_batch(vdev);

    if (msix_set_vector_notifiers(&vdev->pdev, vfio_msix_vector_use,
                                  vfio_msix_vector_release, NULL)) {
        error_report("vfio: msix_set_vector_notifiers failed");
    }

    vfio_pci_commit_kvm_msi_virq_batch(vdev);

    if (vdev->nr_vectors) {
        ret = vfio_enable_vectors(vdev, true);
        if (ret) {
            error_report("vfio: failed to enable vectors, %s",
                         strerror(-ret));
        }
    } else {
        /*
         * Some communication channels between VF & PF or PF & fw rely on the
         * physical state of the device and expect that enabling MSI-X from the
         * guest enables the same on the host.  When our guest is Linux, the
         * guest driver call to pci_enable_msix() sets the enabling bit in the
         * MSI-X capability, but leaves the vector table masked.  We therefore
         * can't rely on a vector_use callback (from request_irq() in the guest)
         * to switch the physical device into MSI-X mode because that may come a
         * long time after pci_enable_msix().  This code sets vector 0 with an
         * invalid fd to make the physical device MSI-X enabled, but with no
         * vectors enabled, just like the guest view.
         */
        ret = vfio_enable_msix_no_vec(vdev);
        if (ret) {
            error_report("vfio: failed to enable MSI-X, %s",
                         strerror(-ret));
        }
    }

    trace_vfio_msix_enable(vdev->vbasedev.name);
}

static void vfio_msi_enable(VFIOPCIDevice *vdev)
{
    int ret, i;

    vfio_disable_interrupts(vdev);

    vdev->nr_vectors = msi_nr_vectors_allocated(&vdev->pdev);
retry:
    /*
     * Setting vector notifiers needs to enable route for each vector.
     * Deferring to commit the KVM routes once rather than per vector
     * provides a substantial performance improvement.
     */
    vfio_pci_prepare_kvm_msi_virq_batch(vdev);

    vdev->msi_vectors = g_new0(VFIOMSIVector, vdev->nr_vectors);

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        Error *local_err = NULL;

        vector->vdev = vdev;
        vector->virq = -1;
        vector->use = true;

        if (!vfio_notifier_init(vdev, &vector->interrupt, "interrupt", i,
                                &local_err)) {
            error_report_err(local_err);
        }

        qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                            vfio_msi_interrupt, NULL, vector);

        /*
         * Attempt to enable route through KVM irqchip,
         * default to userspace handling if unavailable.
         */
        vfio_pci_add_kvm_msi_virq(vdev, vector, i, false);
    }

    vfio_pci_commit_kvm_msi_virq_batch(vdev);

    /* Set interrupt type prior to possible interrupts */
    vdev->interrupt = VFIO_INT_MSI;

    ret = vfio_enable_vectors(vdev, false);
    if (ret) {
        if (ret < 0) {
            error_report("vfio: Error: Failed to setup MSI fds: %s",
                         strerror(-ret));
        } else {
            error_report("vfio: Error: Failed to enable %d "
                         "MSI vectors, retry with %d", vdev->nr_vectors, ret);
        }

        vfio_msi_disable_common(vdev);

        if (ret > 0) {
            vdev->nr_vectors = ret;
            goto retry;
        }

        /*
         * Failing to setup MSI doesn't really fall within any specification.
         * Let's try leaving interrupts disabled and hope the guest figures
         * out to fall back to INTx for this device.
         */
        error_report("vfio: Error: Failed to enable MSI");

        return;
    }

    trace_vfio_msi_enable(vdev->vbasedev.name, vdev->nr_vectors);
}

static void vfio_msi_disable_common(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        if (vdev->msi_vectors[i].use) {
            if (vector->virq >= 0) {
                vfio_remove_kvm_msi_virq(vdev, vector, i);
            }
            qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                                NULL, NULL, NULL);
            vfio_notifier_cleanup(vdev, &vector->interrupt, "interrupt", i);
        }
    }

    g_free(vdev->msi_vectors);
    vdev->msi_vectors = NULL;
    vdev->nr_vectors = 0;
    vdev->interrupt = VFIO_INT_NONE;
}

static void vfio_msix_disable(VFIOPCIDevice *vdev)
{
    Error *err = NULL;
    int i;

    msix_unset_vector_notifiers(&vdev->pdev);

    /*
     * MSI-X will only release vectors if MSI-X is still enabled on the
     * device, check through the rest and release it ourselves if necessary.
     */
    for (i = 0; i < vdev->nr_vectors; i++) {
        if (vdev->msi_vectors[i].use) {
            vfio_msix_vector_release(&vdev->pdev, i);
            msix_vector_unuse(&vdev->pdev, i);
        }
    }

    /*
     * Always clear MSI-X IRQ index. A PF device could have enabled
     * MSI-X with no vectors. See vfio_msix_enable().
     */
    vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX);

    vfio_msi_disable_common(vdev);
    if (!vfio_intx_enable(vdev, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    memset(vdev->msix->pending, 0,
           BITS_TO_LONGS(vdev->msix->entries) * sizeof(unsigned long));

    trace_vfio_msix_disable(vdev->vbasedev.name);
}

static void vfio_msi_disable(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_MSI_IRQ_INDEX);
    vfio_msi_disable_common(vdev);
    vfio_intx_enable(vdev, &err);
    if (err) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    trace_vfio_msi_disable(vdev->vbasedev.name);
}

static void vfio_update_msi(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        MSIMessage msg;

        if (!vector->use || vector->virq < 0) {
            continue;
        }

        msg = msi_get_message(&vdev->pdev, i);
        vfio_update_kvm_msi_virq(vector, msg, &vdev->pdev);
    }
}

static void vfio_pci_load_rom(VFIOPCIDevice *vdev)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info = NULL;
    uint64_t size;
    off_t off = 0;
    ssize_t bytes;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, VFIO_PCI_ROM_REGION_INDEX,
                                      &reg_info);

    if (ret != 0) {
        error_report("vfio: Error getting ROM info: %s", strerror(-ret));
        return;
    }

    trace_vfio_pci_load_rom(vbasedev->name, (unsigned long)reg_info->size,
                            (unsigned long)reg_info->offset,
                            (unsigned long)reg_info->flags);

    vdev->rom_size = size = reg_info->size;
    vdev->rom_offset = reg_info->offset;

    if (!vdev->rom_size) {
        vdev->rom_read_failed = true;
        error_report("vfio-pci: Cannot read device rom at %s", vbasedev->name);
        error_printf("Device option ROM contents are probably invalid "
                    "(check dmesg).\nSkip option ROM probe with rombar=0, "
                    "or load from file with romfile=\n");
        return;
    }

    vdev->rom = g_malloc(size);
    memset(vdev->rom, 0xff, size);

    while (size) {
        bytes = vbasedev->io_ops->region_read(vbasedev,
                                              VFIO_PCI_ROM_REGION_INDEX,
                                              off, size, vdev->rom + off);

        if (bytes == 0) {
            break;
        } else if (bytes > 0) {
            off += bytes;
            size -= bytes;
        } else {
            if (bytes == -EINTR || bytes == -EAGAIN) {
                continue;
            }
            error_report("vfio: Error reading device ROM: %s",
                         strreaderror(bytes));

            break;
        }
    }

    /*
     * Test the ROM signature against our device, if the vendor is correct
     * but the device ID doesn't match, store the correct device ID and
     * recompute the checksum.  Intel IGD devices need this and are known
     * to have bogus checksums so we can't simply adjust the checksum.
     */
    if (pci_get_word(vdev->rom) == 0xaa55 &&
        pci_get_word(vdev->rom + 0x18) + 8 < vdev->rom_size &&
        !memcmp(vdev->rom + pci_get_word(vdev->rom + 0x18), "PCIR", 4)) {
        uint16_t vid, did;

        vid = pci_get_word(vdev->rom + pci_get_word(vdev->rom + 0x18) + 4);
        did = pci_get_word(vdev->rom + pci_get_word(vdev->rom + 0x18) + 6);

        if (vid == vdev->vendor_id && did != vdev->device_id) {
            int i;
            uint8_t csum, *data = vdev->rom;

            pci_set_word(vdev->rom + pci_get_word(vdev->rom + 0x18) + 6,
                         vdev->device_id);
            data[6] = 0;

            for (csum = 0, i = 0; i < vdev->rom_size; i++) {
                csum += data[i];
            }

            data[6] = -csum;
        }
    }
}

/* "Raw" read of underlying config space. */
static int vfio_pci_config_space_read(VFIOPCIDevice *vdev, off_t offset,
                                      uint32_t size, void *data)
{
    return vdev->vbasedev.io_ops->region_read(&vdev->vbasedev,
                                              VFIO_PCI_CONFIG_REGION_INDEX,
                                              offset, size, data);
}

/* "Raw" write of underlying config space. */
static int vfio_pci_config_space_write(VFIOPCIDevice *vdev, off_t offset,
                                       uint32_t size, void *data)
{
    return vdev->vbasedev.io_ops->region_write(&vdev->vbasedev,
                                               VFIO_PCI_CONFIG_REGION_INDEX,
                                               offset, size, data, false);
}

static uint64_t vfio_rom_read(void *opaque, hwaddr addr, unsigned size)
{
    VFIOPCIDevice *vdev = opaque;
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } val;
    uint64_t data = 0;

    /* Load the ROM lazily when the guest tries to read it */
    if (unlikely(!vdev->rom && !vdev->rom_read_failed)) {
        vfio_pci_load_rom(vdev);
    }

    memcpy(&val, vdev->rom + addr,
           (addr < vdev->rom_size) ? MIN(size, vdev->rom_size - addr) : 0);

    switch (size) {
    case 1:
        data = val.byte;
        break;
    case 2:
        data = le16_to_cpu(val.word);
        break;
    case 4:
        data = le32_to_cpu(val.dword);
        break;
    default:
        hw_error("vfio: unsupported read size, %d bytes\n", size);
        break;
    }

    trace_vfio_rom_read(vdev->vbasedev.name, addr, size, data);

    return data;
}

static void vfio_rom_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size)
{
}

static const MemoryRegionOps vfio_rom_ops = {
    .read = vfio_rom_read,
    .write = vfio_rom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_pci_size_rom(VFIOPCIDevice *vdev)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t orig, size = cpu_to_le32((uint32_t)PCI_ROM_ADDRESS_MASK);
    char *name;

    if (vdev->pdev.romfile || !vdev->pdev.rom_bar) {
        /* Since pci handles romfile, just print a message and return */
        if (vfio_opt_rom_in_denylist(vdev) && vdev->pdev.romfile) {
            warn_report("Device at %s is known to cause system instability"
                        " issues during option rom execution",
                        vdev->vbasedev.name);
            error_printf("Proceeding anyway since user specified romfile\n");
        }
        return;
    }

    /*
     * Use the same size ROM BAR as the physical device.  The contents
     * will get filled in later when the guest tries to read it.
     */
    if (vfio_pci_config_space_read(vdev, PCI_ROM_ADDRESS, 4, &orig) != 4 ||
        vfio_pci_config_space_write(vdev, PCI_ROM_ADDRESS, 4, &size) != 4 ||
        vfio_pci_config_space_read(vdev, PCI_ROM_ADDRESS, 4, &size) != 4 ||
        vfio_pci_config_space_write(vdev, PCI_ROM_ADDRESS, 4, &orig) != 4) {

        error_report("%s(%s) ROM access failed", __func__, vbasedev->name);
        return;
    }

    size = ~(le32_to_cpu(size) & PCI_ROM_ADDRESS_MASK) + 1;

    if (!size) {
        return;
    }

    if (vfio_opt_rom_in_denylist(vdev)) {
        if (vdev->pdev.rom_bar > 0) {
            warn_report("Device at %s is known to cause system instability"
                        " issues during option rom execution",
                        vdev->vbasedev.name);
            error_printf("Proceeding anyway since user specified"
                         " positive value for rombar\n");
        } else {
            warn_report("Rom loading for device at %s has been disabled"
                        " due to system instability issues",
                        vdev->vbasedev.name);
            error_printf("Specify rombar=1 or romfile to force\n");
            return;
        }
    }

    trace_vfio_pci_size_rom(vdev->vbasedev.name, size);

    name = g_strdup_printf("vfio[%s].rom", vdev->vbasedev.name);

    memory_region_init_io(&vdev->pdev.rom, OBJECT(vdev),
                          &vfio_rom_ops, vdev, name, size);
    g_free(name);

    pci_register_bar(&vdev->pdev, PCI_ROM_SLOT,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &vdev->pdev.rom);

    vdev->rom_read_failed = false;
}

void vfio_vga_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size)
{
    VFIOVGARegion *region = opaque;
    VFIOVGA *vga = container_of(region, VFIOVGA, region[region->nr]);
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } buf;
    off_t offset = vga->fd_offset + region->offset + addr;

    switch (size) {
    case 1:
        buf.byte = data;
        break;
    case 2:
        buf.word = cpu_to_le16(data);
        break;
    case 4:
        buf.dword = cpu_to_le32(data);
        break;
    default:
        hw_error("vfio: unsupported write size, %d bytes", size);
        break;
    }

    if (pwrite(vga->fd, &buf, size, offset) != size) {
        error_report("%s(,0x%"HWADDR_PRIx", 0x%"PRIx64", %d) failed: %m",
                     __func__, region->offset + addr, data, size);
    }

    trace_vfio_vga_write(region->offset + addr, data, size);
}

uint64_t vfio_vga_read(void *opaque, hwaddr addr, unsigned size)
{
    VFIOVGARegion *region = opaque;
    VFIOVGA *vga = container_of(region, VFIOVGA, region[region->nr]);
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } buf;
    uint64_t data = 0;
    off_t offset = vga->fd_offset + region->offset + addr;

    if (pread(vga->fd, &buf, size, offset) != size) {
        error_report("%s(,0x%"HWADDR_PRIx", %d) failed: %m",
                     __func__, region->offset + addr, size);
        return (uint64_t)-1;
    }

    switch (size) {
    case 1:
        data = buf.byte;
        break;
    case 2:
        data = le16_to_cpu(buf.word);
        break;
    case 4:
        data = le32_to_cpu(buf.dword);
        break;
    default:
        hw_error("vfio: unsupported read size, %d bytes", size);
        break;
    }

    trace_vfio_vga_read(region->offset + addr, size, data);

    return data;
}

static const MemoryRegionOps vfio_vga_ops = {
    .read = vfio_vga_read,
    .write = vfio_vga_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * Expand memory region of sub-page(size < PAGE_SIZE) MMIO BAR to page
 * size if the BAR is in an exclusive page in host so that we could map
 * this BAR to guest. But this sub-page BAR may not occupy an exclusive
 * page in guest. So we should set the priority of the expanded memory
 * region to zero in case of overlap with BARs which share the same page
 * with the sub-page BAR in guest. Besides, we should also recover the
 * size of this sub-page BAR when its base address is changed in guest
 * and not page aligned any more.
 */
static void vfio_sub_page_bar_update_mapping(PCIDevice *pdev, int bar)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIORegion *region = &vdev->bars[bar].region;
    MemoryRegion *mmap_mr, *region_mr, *base_mr;
    PCIIORegion *r;
    pcibus_t bar_addr;
    uint64_t size = region->size;

    /* Make sure that the whole region is allowed to be mmapped */
    if (region->nr_mmaps != 1 || !region->mmaps[0].mmap ||
        region->mmaps[0].size != region->size) {
        return;
    }

    r = &pdev->io_regions[bar];
    bar_addr = r->addr;
    base_mr = vdev->bars[bar].mr;
    region_mr = region->mem;
    mmap_mr = &region->mmaps[0].mem;

    /* If BAR is mapped and page aligned, update to fill PAGE_SIZE */
    if (bar_addr != PCI_BAR_UNMAPPED &&
        !(bar_addr & ~qemu_real_host_page_mask())) {
        size = qemu_real_host_page_size();
    }

    memory_region_transaction_begin();

    if (vdev->bars[bar].size < size) {
        memory_region_set_size(base_mr, size);
    }
    memory_region_set_size(region_mr, size);
    memory_region_set_size(mmap_mr, size);
    if (size != vdev->bars[bar].size && memory_region_is_mapped(base_mr)) {
        memory_region_del_subregion(r->address_space, base_mr);
        memory_region_add_subregion_overlap(r->address_space,
                                            bar_addr, base_mr, 0);
    }

    memory_region_transaction_commit();
}

/*
 * PCI config space
 */
uint32_t vfio_pci_read_config(PCIDevice *pdev, uint32_t addr, int len)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t emu_bits = 0, emu_val = 0, phys_val = 0, val;

    memcpy(&emu_bits, vdev->emulated_config_bits + addr, len);
    emu_bits = le32_to_cpu(emu_bits);

    if (emu_bits) {
        emu_val = pci_default_read_config(pdev, addr, len);
    }

    if (~emu_bits & (0xffffffffU >> (32 - len * 8))) {
        ssize_t ret;

        ret = vfio_pci_config_space_read(vdev, addr, len, &phys_val);
        if (ret != len) {
            error_report("%s(%s, 0x%x, 0x%x) failed: %s",
                         __func__, vbasedev->name, addr, len,
                         strreaderror(ret));
            return -1;
        }
        phys_val = le32_to_cpu(phys_val);
    }

    val = (emu_val & emu_bits) | (phys_val & ~emu_bits);

    trace_vfio_pci_read_config(vdev->vbasedev.name, addr, len, val);

    return val;
}

void vfio_pci_write_config(PCIDevice *pdev,
                           uint32_t addr, uint32_t val, int len)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t val_le = cpu_to_le32(val);
    int ret;

    trace_vfio_pci_write_config(vdev->vbasedev.name, addr, val, len);

    /* Write everything to VFIO, let it filter out what we can't write */
    ret = vfio_pci_config_space_write(vdev, addr, len, &val_le);
    if (ret != len) {
        error_report("%s(%s, 0x%x, 0x%x, 0x%x) failed: %s",
                     __func__, vbasedev->name, addr, val, len,
                    strwriteerror(ret));
    }

    /* MSI/MSI-X Enabling/Disabling */
    if (pdev->cap_present & QEMU_PCI_CAP_MSI &&
        ranges_overlap(addr, len, pdev->msi_cap, vdev->msi_cap_size)) {
        int is_enabled, was_enabled = msi_enabled(pdev);

        pci_default_write_config(pdev, addr, val, len);

        is_enabled = msi_enabled(pdev);

        if (!was_enabled) {
            if (is_enabled) {
                vfio_msi_enable(vdev);
            }
        } else {
            if (!is_enabled) {
                vfio_msi_disable(vdev);
            } else {
                vfio_update_msi(vdev);
            }
        }
    } else if (pdev->cap_present & QEMU_PCI_CAP_MSIX &&
        ranges_overlap(addr, len, pdev->msix_cap, MSIX_CAP_LENGTH)) {
        int is_enabled, was_enabled = msix_enabled(pdev);

        pci_default_write_config(pdev, addr, val, len);

        is_enabled = msix_enabled(pdev);

        if (!was_enabled && is_enabled) {
            vfio_msix_enable(vdev);
        } else if (was_enabled && !is_enabled) {
            vfio_msix_disable(vdev);
        }
    } else if (ranges_overlap(addr, len, PCI_BASE_ADDRESS_0, 24) ||
        range_covers_byte(addr, len, PCI_COMMAND)) {
        pcibus_t old_addr[PCI_NUM_REGIONS - 1];
        int bar;

        for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
            old_addr[bar] = pdev->io_regions[bar].addr;
        }

        pci_default_write_config(pdev, addr, val, len);

        for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
            if (old_addr[bar] != pdev->io_regions[bar].addr &&
                vdev->bars[bar].region.size > 0 &&
                vdev->bars[bar].region.size < qemu_real_host_page_size()) {
                vfio_sub_page_bar_update_mapping(pdev, bar);
            }
        }
    } else {
        /* Write everything to QEMU to keep emulated bits correct */
        pci_default_write_config(pdev, addr, val, len);
    }
}

/*
 * Interrupt setup
 */
static void vfio_disable_interrupts(VFIOPCIDevice *vdev)
{
    /*
     * More complicated than it looks.  Disabling MSI/X transitions the
     * device to INTx mode (if supported).  Therefore we need to first
     * disable MSI/X and then cleanup by disabling INTx.
     */
    if (vdev->interrupt == VFIO_INT_MSIX) {
        vfio_msix_disable(vdev);
    } else if (vdev->interrupt == VFIO_INT_MSI) {
        vfio_msi_disable(vdev);
    }

    if (vdev->interrupt == VFIO_INT_INTx) {
        vfio_intx_disable(vdev);
    }
}

static bool vfio_msi_setup(VFIOPCIDevice *vdev, int pos, Error **errp)
{
    uint16_t ctrl;
    bool msi_64bit, msi_maskbit;
    int ret, entries;
    Error *err = NULL;

    ret = vfio_pci_config_space_read(vdev, pos + PCI_CAP_FLAGS,
                                     sizeof(ctrl), &ctrl);
    if (ret != sizeof(ctrl)) {
        error_setg(errp, "failed reading MSI PCI_CAP_FLAGS: %s",
                   strreaderror(ret));
        return false;
    }
    ctrl = le16_to_cpu(ctrl);

    msi_64bit = !!(ctrl & PCI_MSI_FLAGS_64BIT);
    msi_maskbit = !!(ctrl & PCI_MSI_FLAGS_MASKBIT);
    entries = 1 << ((ctrl & PCI_MSI_FLAGS_QMASK) >> 1);

    trace_vfio_msi_setup(vdev->vbasedev.name, pos);

    ret = msi_init(&vdev->pdev, pos, entries, msi_64bit, msi_maskbit, &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            return true;
        }
        error_propagate_prepend(errp, err, "msi_init failed: ");
        return false;
    }
    vdev->msi_cap_size = 0xa + (msi_maskbit ? 0xa : 0) + (msi_64bit ? 0x4 : 0);

    return true;
}

static void vfio_pci_fixup_msix_region(VFIOPCIDevice *vdev)
{
    off_t start, end;
    VFIORegion *region = &vdev->bars[vdev->msix->table_bar].region;

    /*
     * If the host driver allows mapping of a MSIX data, we are going to
     * do map the entire BAR and emulate MSIX table on top of that.
     */
    if (vfio_device_has_region_cap(&vdev->vbasedev, region->nr,
                                   VFIO_REGION_INFO_CAP_MSIX_MAPPABLE)) {
        return;
    }

    /*
     * We expect to find a single mmap covering the whole BAR, anything else
     * means it's either unsupported or already setup.
     */
    if (region->nr_mmaps != 1 || region->mmaps[0].offset ||
        region->size != region->mmaps[0].size) {
        return;
    }

    /* MSI-X table start and end aligned to host page size */
    start = vdev->msix->table_offset & qemu_real_host_page_mask();
    end = REAL_HOST_PAGE_ALIGN((uint64_t)vdev->msix->table_offset +
                               (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE));

    /*
     * Does the MSI-X table cover the beginning of the BAR?  The whole BAR?
     * NB - Host page size is necessarily a power of two and so is the PCI
     * BAR (not counting EA yet), therefore if we have host page aligned
     * @start and @end, then any remainder of the BAR before or after those
     * must be at least host page sized and therefore mmap'able.
     */
    if (!start) {
        if (end >= region->size) {
            region->nr_mmaps = 0;
            g_free(region->mmaps);
            region->mmaps = NULL;
            trace_vfio_msix_fixup(vdev->vbasedev.name,
                                  vdev->msix->table_bar, 0, 0);
        } else {
            region->mmaps[0].offset = end;
            region->mmaps[0].size = region->size - end;
            trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[0].offset,
                              region->mmaps[0].offset + region->mmaps[0].size);
        }

    /* Maybe it's aligned at the end of the BAR */
    } else if (end >= region->size) {
        region->mmaps[0].size = start;
        trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[0].offset,
                              region->mmaps[0].offset + region->mmaps[0].size);

    /* Otherwise it must split the BAR */
    } else {
        region->nr_mmaps = 2;
        region->mmaps = g_renew(VFIOMmap, region->mmaps, 2);

        memcpy(&region->mmaps[1], &region->mmaps[0], sizeof(VFIOMmap));

        region->mmaps[0].size = start;
        trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[0].offset,
                              region->mmaps[0].offset + region->mmaps[0].size);

        region->mmaps[1].offset = end;
        region->mmaps[1].size = region->size - end;
        trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[1].offset,
                              region->mmaps[1].offset + region->mmaps[1].size);
    }
}

static bool vfio_pci_relocate_msix(VFIOPCIDevice *vdev, Error **errp)
{
    int target_bar = -1;
    size_t msix_sz;

    if (!vdev->msix || vdev->msix_relo == OFF_AUTO_PCIBAR_OFF) {
        return true;
    }

    /* The actual minimum size of MSI-X structures */
    msix_sz = (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE) +
              (QEMU_ALIGN_UP(vdev->msix->entries, 64) / 8);
    /* Round up to host pages, we don't want to share a page */
    msix_sz = REAL_HOST_PAGE_ALIGN(msix_sz);
    /* PCI BARs must be a power of 2 */
    msix_sz = pow2ceil(msix_sz);

    if (vdev->msix_relo == OFF_AUTO_PCIBAR_AUTO) {
        /*
         * TODO: Lookup table for known devices.
         *
         * Logically we might use an algorithm here to select the BAR adding
         * the least additional MMIO space, but we cannot programmatically
         * predict the driver dependency on BAR ordering or sizing, therefore
         * 'auto' becomes a lookup for combinations reported to work.
         */
        if (target_bar < 0) {
            error_setg(errp, "No automatic MSI-X relocation available for "
                       "device %04x:%04x", vdev->vendor_id, vdev->device_id);
            return false;
        }
    } else {
        target_bar = (int)(vdev->msix_relo - OFF_AUTO_PCIBAR_BAR0);
    }

    /* I/O port BARs cannot host MSI-X structures */
    if (vdev->bars[target_bar].ioport) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "I/O port BAR", target_bar);
        return false;
    }

    /* Cannot use a BAR in the "shadow" of a 64-bit BAR */
    if (!vdev->bars[target_bar].size &&
         target_bar > 0 && vdev->bars[target_bar - 1].mem64) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "consumed by 64-bit BAR %d", target_bar, target_bar - 1);
        return false;
    }

    /* 2GB max size for 32-bit BARs, cannot double if already > 1G */
    if (vdev->bars[target_bar].size > 1 * GiB &&
        !vdev->bars[target_bar].mem64) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "no space to extend 32-bit BAR", target_bar);
        return false;
    }

    /*
     * If adding a new BAR, test if we can make it 64bit.  We make it
     * prefetchable since QEMU MSI-X emulation has no read side effects
     * and doing so makes mapping more flexible.
     */
    if (!vdev->bars[target_bar].size) {
        if (target_bar < (PCI_ROM_SLOT - 1) &&
            !vdev->bars[target_bar + 1].size) {
            vdev->bars[target_bar].mem64 = true;
            vdev->bars[target_bar].type = PCI_BASE_ADDRESS_MEM_TYPE_64;
        }
        vdev->bars[target_bar].type |= PCI_BASE_ADDRESS_MEM_PREFETCH;
        vdev->bars[target_bar].size = msix_sz;
        vdev->msix->table_offset = 0;
    } else {
        vdev->bars[target_bar].size = MAX(vdev->bars[target_bar].size * 2,
                                          msix_sz * 2);
        /*
         * Due to above size calc, MSI-X always starts halfway into the BAR,
         * which will always be a separate host page.
         */
        vdev->msix->table_offset = vdev->bars[target_bar].size / 2;
    }

    vdev->msix->table_bar = target_bar;
    vdev->msix->pba_bar = target_bar;
    /* Requires 8-byte alignment, but PCI_MSIX_ENTRY_SIZE guarantees that */
    vdev->msix->pba_offset = vdev->msix->table_offset +
                                  (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE);

    trace_vfio_msix_relo(vdev->vbasedev.name,
                         vdev->msix->table_bar, vdev->msix->table_offset);
    return true;
}

/*
 * We don't have any control over how pci_add_capability() inserts
 * capabilities into the chain.  In order to setup MSI-X we need a
 * MemoryRegion for the BAR.  In order to setup the BAR and not
 * attempt to mmap the MSI-X table area, which VFIO won't allow, we
 * need to first look for where the MSI-X table lives.  So we
 * unfortunately split MSI-X setup across two functions.
 */
static bool vfio_msix_early_setup(VFIOPCIDevice *vdev, Error **errp)
{
    uint8_t pos;
    uint16_t ctrl;
    uint32_t table, pba;
    struct vfio_irq_info irq_info;
    VFIOMSIXInfo *msix;
    int ret;

    pos = pci_find_capability(&vdev->pdev, PCI_CAP_ID_MSIX);
    if (!pos) {
        return true;
    }

    ret = vfio_pci_config_space_read(vdev, pos + PCI_MSIX_FLAGS,
                                     sizeof(ctrl), &ctrl);
    if (ret != sizeof(ctrl)) {
        error_setg(errp, "failed to read PCI MSIX FLAGS: %s",
                   strreaderror(ret));
        return false;
    }

    ret = vfio_pci_config_space_read(vdev, pos + PCI_MSIX_TABLE,
                                     sizeof(table), &table);
    if (ret != sizeof(table)) {
        error_setg(errp, "failed to read PCI MSIX TABLE: %s",
                   strreaderror(ret));
        return false;
    }

    ret = vfio_pci_config_space_read(vdev, pos + PCI_MSIX_PBA,
                                     sizeof(pba), &pba);
    if (ret != sizeof(pba)) {
        error_setg(errp, "failed to read PCI MSIX PBA: %s", strreaderror(ret));
        return false;
    }

    ctrl = le16_to_cpu(ctrl);
    table = le32_to_cpu(table);
    pba = le32_to_cpu(pba);

    msix = g_malloc0(sizeof(*msix));
    msix->table_bar = table & PCI_MSIX_FLAGS_BIRMASK;
    msix->table_offset = table & ~PCI_MSIX_FLAGS_BIRMASK;
    msix->pba_bar = pba & PCI_MSIX_FLAGS_BIRMASK;
    msix->pba_offset = pba & ~PCI_MSIX_FLAGS_BIRMASK;
    msix->entries = (ctrl & PCI_MSIX_FLAGS_QSIZE) + 1;

    ret = vfio_device_get_irq_info(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX,
                                   &irq_info);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get MSI-X irq info");
        g_free(msix);
        return false;
    }

    msix->noresize = !!(irq_info.flags & VFIO_IRQ_INFO_NORESIZE);

    /*
     * Test the size of the pba_offset variable and catch if it extends outside
     * of the specified BAR. If it is the case, we need to apply a hardware
     * specific quirk if the device is known or we have a broken configuration.
     */
    if (msix->pba_offset >= vdev->bars[msix->pba_bar].region.size) {
        /*
         * Chelsio T5 Virtual Function devices are encoded as 0x58xx for T5
         * adapters. The T5 hardware returns an incorrect value of 0x8000 for
         * the VF PBA offset while the BAR itself is only 8k. The correct value
         * is 0x1000, so we hard code that here.
         */
        if (vdev->vendor_id == PCI_VENDOR_ID_CHELSIO &&
            (vdev->device_id & 0xff00) == 0x5800) {
            msix->pba_offset = 0x1000;
        /*
         * BAIDU KUNLUN Virtual Function devices for KUNLUN AI processor
         * return an incorrect value of 0x460000 for the VF PBA offset while
         * the BAR itself is only 0x10000.  The correct value is 0xb400.
         */
        } else if (vfio_pci_is(vdev, PCI_VENDOR_ID_BAIDU,
                               PCI_DEVICE_ID_KUNLUN_VF)) {
            msix->pba_offset = 0xb400;
        } else if (vdev->msix_relo == OFF_AUTO_PCIBAR_OFF) {
            error_setg(errp, "hardware reports invalid configuration, "
                       "MSIX PBA outside of specified BAR");
            g_free(msix);
            return false;
        }
    }

    trace_vfio_msix_early_setup(vdev->vbasedev.name, pos, msix->table_bar,
                                msix->table_offset, msix->entries,
                                msix->noresize);
    vdev->msix = msix;

    vfio_pci_fixup_msix_region(vdev);

    return vfio_pci_relocate_msix(vdev, errp);
}

static bool vfio_msix_setup(VFIOPCIDevice *vdev, int pos, Error **errp)
{
    int ret;
    Error *err = NULL;

    vdev->msix->pending = g_new0(unsigned long,
                                 BITS_TO_LONGS(vdev->msix->entries));
    ret = msix_init(&vdev->pdev, vdev->msix->entries,
                    vdev->bars[vdev->msix->table_bar].mr,
                    vdev->msix->table_bar, vdev->msix->table_offset,
                    vdev->bars[vdev->msix->pba_bar].mr,
                    vdev->msix->pba_bar, vdev->msix->pba_offset, pos,
                    &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            warn_report_err(err);
            return true;
        }

        error_propagate(errp, err);
        return false;
    }

    /*
     * The PCI spec suggests that devices provide additional alignment for
     * MSI-X structures and avoid overlapping non-MSI-X related registers.
     * For an assigned device, this hopefully means that emulation of MSI-X
     * structures does not affect the performance of the device.  If devices
     * fail to provide that alignment, a significant performance penalty may
     * result, for instance Mellanox MT27500 VFs:
     * http://www.spinics.net/lists/kvm/msg125881.html
     *
     * The PBA is simply not that important for such a serious regression and
     * most drivers do not appear to look at it.  The solution for this is to
     * disable the PBA MemoryRegion unless it's being used.  We disable it
     * here and only enable it if a masked vector fires through QEMU.  As the
     * vector-use notifier is called, which occurs on unmask, we test whether
     * PBA emulation is needed and again disable if not.
     */
    memory_region_set_enabled(&vdev->pdev.msix_pba_mmio, false);

    /*
     * The emulated machine may provide a paravirt interface for MSIX setup
     * so it is not strictly necessary to emulate MSIX here. This becomes
     * helpful when frequently accessed MMIO registers are located in
     * subpages adjacent to the MSIX table but the MSIX data containing page
     * cannot be mapped because of a host page size bigger than the MSIX table
     * alignment.
     */
    if (object_property_get_bool(OBJECT(qdev_get_machine()),
                                 "vfio-no-msix-emulation", NULL)) {
        memory_region_set_enabled(&vdev->pdev.msix_table_mmio, false);
    }

    return true;
}

void vfio_pci_teardown_msi(VFIOPCIDevice *vdev)
{
    msi_uninit(&vdev->pdev);

    if (vdev->msix) {
        msix_uninit(&vdev->pdev,
                    vdev->bars[vdev->msix->table_bar].mr,
                    vdev->bars[vdev->msix->pba_bar].mr);
        g_free(vdev->msix->pending);
    }
}

/*
 * Resource setup
 */
static void vfio_mmap_set_enabled(VFIOPCIDevice *vdev, bool enabled)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_region_mmaps_set_enabled(&vdev->bars[i].region, enabled);
    }
}

static void vfio_bar_prepare(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];

    uint32_t pci_bar;
    int ret;

    /* Skip both unimplemented BARs and the upper half of 64bit BARS. */
    if (!bar->region.size) {
        return;
    }

    /* Determine what type of BAR this is for registration */
    ret = vfio_pci_config_space_read(vdev, PCI_BASE_ADDRESS_0 + (4 * nr),
                                     sizeof(pci_bar), &pci_bar);
    if (ret != sizeof(pci_bar)) {
        error_report("vfio: Failed to read BAR %d: %s", nr, strreaderror(ret));
        return;
    }

    pci_bar = le32_to_cpu(pci_bar);
    bar->ioport = (pci_bar & PCI_BASE_ADDRESS_SPACE_IO);
    bar->mem64 = bar->ioport ? 0 : (pci_bar & PCI_BASE_ADDRESS_MEM_TYPE_64);
    bar->type = pci_bar & (bar->ioport ? ~PCI_BASE_ADDRESS_IO_MASK :
                                         ~PCI_BASE_ADDRESS_MEM_MASK);
    bar->size = bar->region.size;

    /* IO regions are sync, memory can be async */
    bar->region.post_wr = (bar->ioport == 0);
}

static void vfio_bars_prepare(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_prepare(vdev, i);
    }
}

static void vfio_bar_register(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];
    char *name;

    if (!bar->size) {
        return;
    }

    bar->mr = g_new0(MemoryRegion, 1);
    name = g_strdup_printf("%s base BAR %d", vdev->vbasedev.name, nr);
    memory_region_init_io(bar->mr, OBJECT(vdev), NULL, NULL, name, bar->size);
    g_free(name);

    if (bar->region.size) {
        memory_region_add_subregion(bar->mr, 0, bar->region.mem);

        if (vfio_region_mmap(&bar->region)) {
            error_report("Failed to mmap %s BAR %d. Performance may be slow",
                         vdev->vbasedev.name, nr);
        }
    }

    pci_register_bar(&vdev->pdev, nr, bar->type, bar->mr);
}

static void vfio_bars_register(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_register(vdev, i);
    }
}

void vfio_pci_bars_exit(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        VFIOBAR *bar = &vdev->bars[i];

        vfio_bar_quirk_exit(vdev, i);
        vfio_region_exit(&bar->region);
        if (bar->region.size) {
            memory_region_del_subregion(bar->mr, bar->region.mem);
        }
    }

    if (vdev->vga) {
        pci_unregister_vga(&vdev->pdev);
        vfio_vga_quirk_exit(vdev);
    }
}

static void vfio_bars_finalize(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        VFIOBAR *bar = &vdev->bars[i];

        vfio_bar_quirk_finalize(vdev, i);
        vfio_region_finalize(&bar->region);
        if (bar->mr) {
            assert(bar->size);
            object_unparent(OBJECT(bar->mr));
            g_free(bar->mr);
            bar->mr = NULL;
        }
    }

    if (vdev->vga) {
        vfio_vga_quirk_finalize(vdev);
        for (i = 0; i < ARRAY_SIZE(vdev->vga->region); i++) {
            object_unparent(OBJECT(&vdev->vga->region[i].mem));
        }
        g_free(vdev->vga);
    }
}

/*
 * General setup
 */
static uint8_t vfio_std_cap_max_size(PCIDevice *pdev, uint8_t pos)
{
    uint8_t tmp;
    uint16_t next = PCI_CONFIG_SPACE_SIZE;

    for (tmp = pdev->config[PCI_CAPABILITY_LIST]; tmp;
         tmp = pdev->config[tmp + PCI_CAP_LIST_NEXT]) {
        if (tmp > pos && tmp < next) {
            next = tmp;
        }
    }

    return next - pos;
}


static uint16_t vfio_ext_cap_max_size(const uint8_t *config, uint16_t pos)
{
    uint16_t tmp, next = PCIE_CONFIG_SPACE_SIZE;

    for (tmp = PCI_CONFIG_SPACE_SIZE; tmp;
        tmp = PCI_EXT_CAP_NEXT(pci_get_long(config + tmp))) {
        if (tmp > pos && tmp < next) {
            next = tmp;
        }
    }

    return next - pos;
}

static void vfio_set_word_bits(uint8_t *buf, uint16_t val, uint16_t mask)
{
    pci_set_word(buf, (pci_get_word(buf) & ~mask) | val);
}

static void vfio_add_emulated_word(VFIOPCIDevice *vdev, int pos,
                                   uint16_t val, uint16_t mask)
{
    vfio_set_word_bits(vdev->pdev.config + pos, val, mask);
    vfio_set_word_bits(vdev->pdev.wmask + pos, ~mask, mask);
    vfio_set_word_bits(vdev->emulated_config_bits + pos, mask, mask);
}

static void vfio_set_long_bits(uint8_t *buf, uint32_t val, uint32_t mask)
{
    pci_set_long(buf, (pci_get_long(buf) & ~mask) | val);
}

static void vfio_add_emulated_long(VFIOPCIDevice *vdev, int pos,
                                   uint32_t val, uint32_t mask)
{
    vfio_set_long_bits(vdev->pdev.config + pos, val, mask);
    vfio_set_long_bits(vdev->pdev.wmask + pos, ~mask, mask);
    vfio_set_long_bits(vdev->emulated_config_bits + pos, mask, mask);
}

static void vfio_pci_enable_rp_atomics(VFIOPCIDevice *vdev)
{
    struct vfio_device_info_cap_pci_atomic_comp *cap;
    g_autofree struct vfio_device_info *info = NULL;
    PCIBus *bus = pci_get_bus(&vdev->pdev);
    PCIDevice *parent = bus->parent_dev;
    struct vfio_info_cap_header *hdr;
    uint32_t mask = 0;
    uint8_t *pos;

    /*
     * PCIe Atomic Ops completer support is only added automatically for single
     * function devices downstream of a root port supporting DEVCAP2.  Support
     * is added during realize and, if added, removed during device exit.  The
     * single function requirement avoids conflicting requirements should a
     * slot be composed of multiple devices with differing capabilities.
     */
    if (pci_bus_is_root(bus) || !parent || !parent->exp.exp_cap ||
        pcie_cap_get_type(parent) != PCI_EXP_TYPE_ROOT_PORT ||
        pcie_cap_get_version(parent) != PCI_EXP_FLAGS_VER2 ||
        vdev->pdev.devfn ||
        vdev->pdev.cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        return;
    }

    pos = parent->config + parent->exp.exp_cap + PCI_EXP_DEVCAP2;

    /* Abort if there'a already an Atomic Ops configuration on the root port */
    if (pci_get_long(pos) & (PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
                             PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
                             PCI_EXP_DEVCAP2_ATOMIC_COMP128)) {
        return;
    }

    info = vfio_get_device_info(vdev->vbasedev.fd);
    if (!info) {
        return;
    }

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_PCI_ATOMIC_COMP);
    if (!hdr) {
        return;
    }

    cap = (void *)hdr;
    if (cap->flags & VFIO_PCI_ATOMIC_COMP32) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP32;
    }
    if (cap->flags & VFIO_PCI_ATOMIC_COMP64) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP64;
    }
    if (cap->flags & VFIO_PCI_ATOMIC_COMP128) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP128;
    }

    if (!mask) {
        return;
    }

    pci_long_test_and_set_mask(pos, mask);
    vdev->clear_parent_atomics_on_exit = true;
}

static void vfio_pci_disable_rp_atomics(VFIOPCIDevice *vdev)
{
    if (vdev->clear_parent_atomics_on_exit) {
        PCIDevice *parent = pci_get_bus(&vdev->pdev)->parent_dev;
        uint8_t *pos = parent->config + parent->exp.exp_cap + PCI_EXP_DEVCAP2;

        pci_long_test_and_clear_mask(pos, PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
                                          PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
                                          PCI_EXP_DEVCAP2_ATOMIC_COMP128);
    }
}

static bool vfio_setup_pcie_cap(VFIOPCIDevice *vdev, int pos, uint8_t size,
                                Error **errp)
{
    uint16_t flags;
    uint8_t type;

    flags = pci_get_word(vdev->pdev.config + pos + PCI_CAP_FLAGS);
    type = (flags & PCI_EXP_FLAGS_TYPE) >> 4;

    if (type != PCI_EXP_TYPE_ENDPOINT &&
        type != PCI_EXP_TYPE_LEG_END &&
        type != PCI_EXP_TYPE_RC_END) {

        error_setg(errp, "assignment of PCIe type 0x%x "
                   "devices is not currently supported", type);
        return false;
    }

    if (!pci_bus_is_express(pci_get_bus(&vdev->pdev))) {
        PCIBus *bus = pci_get_bus(&vdev->pdev);
        PCIDevice *bridge;

        /*
         * Traditionally PCI device assignment exposes the PCIe capability
         * as-is on non-express buses.  The reason being that some drivers
         * simply assume that it's there, for example tg3.  However when
         * we're running on a native PCIe machine type, like Q35, we need
         * to hide the PCIe capability.  The reason for this is twofold;
         * first Windows guests get a Code 10 error when the PCIe capability
         * is exposed in this configuration.  Therefore express devices won't
         * work at all unless they're attached to express buses in the VM.
         * Second, a native PCIe machine introduces the possibility of fine
         * granularity IOMMUs supporting both translation and isolation.
         * Guest code to discover the IOMMU visibility of a device, such as
         * IOMMU grouping code on Linux, is very aware of device types and
         * valid transitions between bus types.  An express device on a non-
         * express bus is not a valid combination on bare metal systems.
         *
         * Drivers that require a PCIe capability to make the device
         * functional are simply going to need to have their devices placed
         * on a PCIe bus in the VM.
         */
        while (!pci_bus_is_root(bus)) {
            bridge = pci_bridge_get_device(bus);
            bus = pci_get_bus(bridge);
        }

        if (pci_bus_is_express(bus)) {
            return true;
        }

    } else if (pci_bus_is_root(pci_get_bus(&vdev->pdev))) {
        /*
         * On a Root Complex bus Endpoints become Root Complex Integrated
         * Endpoints, which changes the type and clears the LNK & LNK2 fields.
         */
        if (type == PCI_EXP_TYPE_ENDPOINT) {
            vfio_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                                   PCI_EXP_TYPE_RC_END << 4,
                                   PCI_EXP_FLAGS_TYPE);

            /* Link Capabilities, Status, and Control goes away */
            if (size > PCI_EXP_LNKCTL) {
                vfio_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP, 0, ~0);
                vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL, 0, ~0);
                vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKSTA, 0, ~0);

#ifndef PCI_EXP_LNKCAP2
#define PCI_EXP_LNKCAP2 44
#endif
#ifndef PCI_EXP_LNKSTA2
#define PCI_EXP_LNKSTA2 50
#endif
                /* Link 2 Capabilities, Status, and Control goes away */
                if (size > PCI_EXP_LNKCAP2) {
                    vfio_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP2, 0, ~0);
                    vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL2, 0, ~0);
                    vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKSTA2, 0, ~0);
                }
            }

        } else if (type == PCI_EXP_TYPE_LEG_END) {
            /*
             * Legacy endpoints don't belong on the root complex.  Windows
             * seems to be happier with devices if we skip the capability.
             */
            return true;
        }

    } else {
        /*
         * Convert Root Complex Integrated Endpoints to regular endpoints.
         * These devices don't support LNK/LNK2 capabilities, so make them up.
         */
        if (type == PCI_EXP_TYPE_RC_END) {
            vfio_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                                   PCI_EXP_TYPE_ENDPOINT << 4,
                                   PCI_EXP_FLAGS_TYPE);
            vfio_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP,
                           QEMU_PCI_EXP_LNKCAP_MLW(QEMU_PCI_EXP_LNK_X1) |
                           QEMU_PCI_EXP_LNKCAP_MLS(QEMU_PCI_EXP_LNK_2_5GT), ~0);
            vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL, 0, ~0);
        }

        vfio_pci_enable_rp_atomics(vdev);
    }

    /*
     * Intel 82599 SR-IOV VFs report an invalid PCIe capability version 0
     * (Niantic errate #35) causing Windows to error with a Code 10 for the
     * device on Q35.  Fixup any such devices to report version 1.  If we
     * were to remove the capability entirely the guest would lose extended
     * config space.
     */
    if ((flags & PCI_EXP_FLAGS_VERS) == 0) {
        vfio_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                               1, PCI_EXP_FLAGS_VERS);
    }

    pos = pci_add_capability(&vdev->pdev, PCI_CAP_ID_EXP, pos, size,
                             errp);
    if (pos < 0) {
        return false;
    }

    vdev->pdev.exp.exp_cap = pos;

    return true;
}

static void vfio_check_pcie_flr(VFIOPCIDevice *vdev, uint8_t pos)
{
    uint32_t cap = pci_get_long(vdev->pdev.config + pos + PCI_EXP_DEVCAP);

    if (cap & PCI_EXP_DEVCAP_FLR) {
        trace_vfio_check_pcie_flr(vdev->vbasedev.name);
        vdev->has_flr = true;
    }
}

static void vfio_check_pm_reset(VFIOPCIDevice *vdev, uint8_t pos)
{
    uint16_t csr = pci_get_word(vdev->pdev.config + pos + PCI_PM_CTRL);

    if (!(csr & PCI_PM_CTRL_NO_SOFT_RESET)) {
        trace_vfio_check_pm_reset(vdev->vbasedev.name);
        vdev->has_pm_reset = true;
    }
}

static void vfio_check_af_flr(VFIOPCIDevice *vdev, uint8_t pos)
{
    uint8_t cap = pci_get_byte(vdev->pdev.config + pos + PCI_AF_CAP);

    if ((cap & PCI_AF_CAP_TP) && (cap & PCI_AF_CAP_FLR)) {
        trace_vfio_check_af_flr(vdev->vbasedev.name);
        vdev->has_flr = true;
    }
}

static bool vfio_add_vendor_specific_cap(VFIOPCIDevice *vdev, int pos,
                                         uint8_t size, Error **errp)
{
    PCIDevice *pdev = &vdev->pdev;

    pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, pos, size, errp);
    if (pos < 0) {
        return false;
    }

    /*
     * Exempt config space check for Vendor Specific Information during
     * restore/load.
     * Config space check is still enforced for 3 byte VSC header.
     */
    if (vdev->skip_vsc_check && size > 3) {
        memset(pdev->cmask + pos + 3, 0, size - 3);
    }

    return true;
}

static bool vfio_add_std_cap(VFIOPCIDevice *vdev, uint8_t pos, Error **errp)
{
    ERRP_GUARD();
    PCIDevice *pdev = &vdev->pdev;
    uint8_t cap_id, next, size;
    bool ret;

    cap_id = pdev->config[pos];
    next = pdev->config[pos + PCI_CAP_LIST_NEXT];

    /*
     * If it becomes important to configure capabilities to their actual
     * size, use this as the default when it's something we don't recognize.
     * Since QEMU doesn't actually handle many of the config accesses,
     * exact size doesn't seem worthwhile.
     */
    size = vfio_std_cap_max_size(pdev, pos);

    /*
     * pci_add_capability always inserts the new capability at the head
     * of the chain.  Therefore to end up with a chain that matches the
     * physical device, we insert from the end by making this recursive.
     * This is also why we pre-calculate size above as cached config space
     * will be changed as we unwind the stack.
     */
    if (next) {
        if (!vfio_add_std_cap(vdev, next, errp)) {
            return false;
        }
    } else {
        /* Begin the rebuild, use QEMU emulated list bits */
        pdev->config[PCI_CAPABILITY_LIST] = 0;
        vdev->emulated_config_bits[PCI_CAPABILITY_LIST] = 0xff;
        vdev->emulated_config_bits[PCI_STATUS] |= PCI_STATUS_CAP_LIST;

        if (!vfio_add_virt_caps(vdev, errp)) {
            return false;
        }
    }

    /* Scale down size, esp in case virt caps were added above */
    size = MIN(size, vfio_std_cap_max_size(pdev, pos));

    /* Use emulated next pointer to allow dropping caps */
    pci_set_byte(vdev->emulated_config_bits + pos + PCI_CAP_LIST_NEXT, 0xff);

    switch (cap_id) {
    case PCI_CAP_ID_MSI:
        ret = vfio_msi_setup(vdev, pos, errp);
        break;
    case PCI_CAP_ID_EXP:
        vfio_check_pcie_flr(vdev, pos);
        ret = vfio_setup_pcie_cap(vdev, pos, size, errp);
        break;
    case PCI_CAP_ID_MSIX:
        ret = vfio_msix_setup(vdev, pos, errp);
        break;
    case PCI_CAP_ID_PM:
        vfio_check_pm_reset(vdev, pos);
        ret = pci_pm_init(pdev, pos, errp) >= 0;
        /*
         * PCI-core config space emulation needs write access to the power
         * state enabled for tracking BAR mapping relative to PM state.
         */
        pci_set_word(pdev->wmask + pos + PCI_PM_CTRL, PCI_PM_CTRL_STATE_MASK);
        break;
    case PCI_CAP_ID_AF:
        vfio_check_af_flr(vdev, pos);
        ret = pci_add_capability(pdev, cap_id, pos, size, errp) >= 0;
        break;
    case PCI_CAP_ID_VNDR:
        ret = vfio_add_vendor_specific_cap(vdev, pos, size, errp);
        break;
    default:
        ret = pci_add_capability(pdev, cap_id, pos, size, errp) >= 0;
        break;
    }

    if (!ret) {
        error_prepend(errp,
                      "failed to add PCI capability 0x%x[0x%x]@0x%x: ",
                      cap_id, size, pos);
    }

    return ret;
}

static int vfio_setup_rebar_ecap(VFIOPCIDevice *vdev, uint16_t pos)
{
    uint32_t ctrl;
    int i, nbar;

    ctrl = pci_get_long(vdev->pdev.config + pos + PCI_REBAR_CTRL);
    nbar = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >> PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbar; i++) {
        uint32_t cap;
        int size;

        ctrl = pci_get_long(vdev->pdev.config + pos + PCI_REBAR_CTRL + (i * 8));
        size = (ctrl & PCI_REBAR_CTRL_BAR_SIZE) >> PCI_REBAR_CTRL_BAR_SHIFT;

        /* The cap register reports sizes 1MB to 128TB, with 4 reserved bits */
        cap = size <= 27 ? 1U << (size + 4) : 0;

        /*
         * The PCIe spec (v6.0.1, 7.8.6) requires HW to support at least one
         * size in the range 1MB to 512GB.  We intend to mask all sizes except
         * the one currently enabled in the size field, therefore if it's
         * outside the range, hide the whole capability as this virtualization
         * trick won't work.  If >512GB resizable BARs start to appear, we
         * might need an opt-in or reservation scheme in the kernel.
         */
        if (!(cap & PCI_REBAR_CAP_SIZES)) {
            return -EINVAL;
        }

        /* Hide all sizes reported in the ctrl reg per above requirement. */
        ctrl &= (PCI_REBAR_CTRL_BAR_SIZE |
                 PCI_REBAR_CTRL_NBAR_MASK |
                 PCI_REBAR_CTRL_BAR_IDX);

        /*
         * The BAR size field is RW, however we've mangled the capability
         * register such that we only report a single size, ie. the current
         * BAR size.  A write of an unsupported value is undefined, therefore
         * the register field is essentially RO.
         */
        vfio_add_emulated_long(vdev, pos + PCI_REBAR_CAP + (i * 8), cap, ~0);
        vfio_add_emulated_long(vdev, pos + PCI_REBAR_CTRL + (i * 8), ctrl, ~0);
    }

    return 0;
}

static void vfio_add_ext_cap(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = &vdev->pdev;
    uint32_t header;
    uint16_t cap_id, next, size;
    uint8_t cap_ver;
    uint8_t *config;

    /* Only add extended caps if we have them and the guest can see them */
    if (!pci_is_express(pdev) || !pci_bus_is_express(pci_get_bus(pdev)) ||
        !pci_get_long(pdev->config + PCI_CONFIG_SPACE_SIZE)) {
        return;
    }

    /*
     * pcie_add_capability always inserts the new capability at the tail
     * of the chain.  Therefore to end up with a chain that matches the
     * physical device, we cache the config space to avoid overwriting
     * the original config space when we parse the extended capabilities.
     */
    config = g_memdup(pdev->config, vdev->config_size);

    /*
     * Extended capabilities are chained with each pointing to the next, so we
     * can drop anything other than the head of the chain simply by modifying
     * the previous next pointer.  Seed the head of the chain here such that
     * we can simply skip any capabilities we want to drop below, regardless
     * of their position in the chain.  If this stub capability still exists
     * after we add the capabilities we want to expose, update the capability
     * ID to zero.  Note that we cannot seed with the capability header being
     * zero as this conflicts with definition of an absent capability chain
     * and prevents capabilities beyond the head of the list from being added.
     * By replacing the dummy capability ID with zero after walking the device
     * chain, we also transparently mark extended capabilities as absent if
     * no capabilities were added.  Note that the PCIe spec defines an absence
     * of extended capabilities to be determined by a value of zero for the
     * capability ID, version, AND next pointer.  A non-zero next pointer
     * should be sufficient to indicate additional capabilities are present,
     * which will occur if we call pcie_add_capability() below.  The entire
     * first dword is emulated to support this.
     *
     * NB. The kernel side does similar masking, so be prepared that our
     * view of the device may also contain a capability ID zero in the head
     * of the chain.  Skip it for the same reason that we cannot seed the
     * chain with a zero capability.
     */
    pci_set_long(pdev->config + PCI_CONFIG_SPACE_SIZE,
                 PCI_EXT_CAP(0xFFFF, 0, 0));
    pci_set_long(pdev->wmask + PCI_CONFIG_SPACE_SIZE, 0);
    pci_set_long(vdev->emulated_config_bits + PCI_CONFIG_SPACE_SIZE, ~0);

    for (next = PCI_CONFIG_SPACE_SIZE; next;
         next = PCI_EXT_CAP_NEXT(pci_get_long(config + next))) {
        header = pci_get_long(config + next);
        cap_id = PCI_EXT_CAP_ID(header);
        cap_ver = PCI_EXT_CAP_VER(header);

        /*
         * If it becomes important to configure extended capabilities to their
         * actual size, use this as the default when it's something we don't
         * recognize. Since QEMU doesn't actually handle many of the config
         * accesses, exact size doesn't seem worthwhile.
         */
        size = vfio_ext_cap_max_size(config, next);

        /* Use emulated next pointer to allow dropping extended caps */
        pci_long_test_and_set_mask(vdev->emulated_config_bits + next,
                                   PCI_EXT_CAP_NEXT_MASK);

        switch (cap_id) {
        case 0: /* kernel masked capability */
        case PCI_EXT_CAP_ID_SRIOV: /* Read-only VF BARs confuse OVMF */
        case PCI_EXT_CAP_ID_ARI: /* XXX Needs next function virtualization */
            trace_vfio_add_ext_cap_dropped(vdev->vbasedev.name, cap_id, next);
            break;
        case PCI_EXT_CAP_ID_REBAR:
            if (!vfio_setup_rebar_ecap(vdev, next)) {
                pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            }
            break;
        default:
            pcie_add_capability(pdev, cap_id, cap_ver, next, size);
        }

    }

    /* Cleanup chain head ID if necessary */
    if (pci_get_word(pdev->config + PCI_CONFIG_SPACE_SIZE) == 0xFFFF) {
        pci_set_word(pdev->config + PCI_CONFIG_SPACE_SIZE, 0);
    }

    g_free(config);
}

bool vfio_pci_add_capabilities(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = &vdev->pdev;

    if (!(pdev->config[PCI_STATUS] & PCI_STATUS_CAP_LIST) ||
        !pdev->config[PCI_CAPABILITY_LIST]) {
        return true; /* Nothing to add */
    }

    if (!vfio_add_std_cap(vdev, pdev->config[PCI_CAPABILITY_LIST], errp)) {
        return false;
    }

    vfio_add_ext_cap(vdev);
    return true;
}

void vfio_pci_pre_reset(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = &vdev->pdev;
    uint16_t cmd;

    vfio_disable_interrupts(vdev);

    /*
     * Stop any ongoing DMA by disconnecting I/O, MMIO, and bus master.
     * Also put INTx Disable in known state.
     */
    cmd = vfio_pci_read_config(pdev, PCI_COMMAND, 2);
    cmd &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
             PCI_COMMAND_INTX_DISABLE);
    vfio_pci_write_config(pdev, PCI_COMMAND, cmd, 2);

    /* Make sure the device is in D0 */
    if (pdev->pm_cap) {
        uint16_t pmcsr;
        uint8_t state;

        pmcsr = vfio_pci_read_config(pdev, pdev->pm_cap + PCI_PM_CTRL, 2);
        state = pmcsr & PCI_PM_CTRL_STATE_MASK;
        if (state) {
            pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
            vfio_pci_write_config(pdev, pdev->pm_cap + PCI_PM_CTRL, pmcsr, 2);
            /* vfio handles the necessary delay here */
            pmcsr = vfio_pci_read_config(pdev, pdev->pm_cap + PCI_PM_CTRL, 2);
            state = pmcsr & PCI_PM_CTRL_STATE_MASK;
            if (state) {
                error_report("vfio: Unable to power on device, stuck in D%d",
                             state);
            }
        }
    }
}

void vfio_pci_post_reset(VFIOPCIDevice *vdev)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    Error *err = NULL;
    int ret, nr;

    if (!vfio_intx_enable(vdev, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    for (nr = 0; nr < PCI_NUM_REGIONS - 1; ++nr) {
        off_t addr = PCI_BASE_ADDRESS_0 + (4 * nr);
        uint32_t val = 0;
        uint32_t len = sizeof(val);

        ret = vfio_pci_config_space_write(vdev, addr, len, &val);
        if (ret != len) {
            error_report("%s(%s) reset bar %d failed: %s", __func__,
                         vbasedev->name, nr, strwriteerror(ret));
        }
    }

    vfio_quirk_reset(vdev);
}

bool vfio_pci_host_match(PCIHostDeviceAddress *addr, const char *name)
{
    char tmp[13];

    sprintf(tmp, "%04x:%02x:%02x.%1x", addr->domain,
            addr->bus, addr->slot, addr->function);

    return (strcmp(tmp, name) == 0);
}

int vfio_pci_get_pci_hot_reset_info(VFIOPCIDevice *vdev,
                                    struct vfio_pci_hot_reset_info **info_p)
{
    struct vfio_pci_hot_reset_info *info;
    int ret, count;

    assert(info_p && !*info_p);

    info = g_malloc0(sizeof(*info));
    info->argsz = sizeof(*info);

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, info);
    if (ret && errno != ENOSPC) {
        ret = -errno;
        g_free(info);
        if (!vdev->has_pm_reset) {
            error_report("vfio: Cannot reset device %s, "
                         "no available reset mechanism.", vdev->vbasedev.name);
        }
        return ret;
    }

    count = info->count;
    info = g_realloc(info, sizeof(*info) + (count * sizeof(info->devices[0])));
    info->argsz = sizeof(*info) + (count * sizeof(info->devices[0]));

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, info);
    if (ret) {
        ret = -errno;
        g_free(info);
        error_report("vfio: hot reset info failed: %m");
        return ret;
    }

    *info_p = info;
    return 0;
}

static int vfio_pci_hot_reset(VFIOPCIDevice *vdev, bool single)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    const VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(vbasedev->bcontainer);

    return vioc->pci_hot_reset(vbasedev, single);
}

/*
 * We want to differentiate hot reset of multiple in-use devices vs hot reset
 * of a single in-use device.  VFIO_DEVICE_RESET will already handle the case
 * of doing hot resets when there is only a single device per bus.  The in-use
 * here refers to how many VFIODevices are affected.  A hot reset that affects
 * multiple devices, but only a single in-use device, means that we can call
 * it from our bus ->reset() callback since the extent is effectively a single
 * device.  This allows us to make use of it in the hotplug path.  When there
 * are multiple in-use devices, we can only trigger the hot reset during a
 * system reset and thus from our reset handler.  We separate _one vs _multi
 * here so that we don't overlap and do a double reset on the system reset
 * path where both our reset handler and ->reset() callback are used.  Calling
 * _one() will only do a hot reset for the one in-use devices case, calling
 * _multi() will do nothing if a _one() would have been sufficient.
 */
static int vfio_pci_hot_reset_one(VFIOPCIDevice *vdev)
{
    return vfio_pci_hot_reset(vdev, true);
}

static int vfio_pci_hot_reset_multi(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    return vfio_pci_hot_reset(vdev, false);
}

static void vfio_pci_compute_needs_reset(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    if (!vbasedev->reset_works || (!vdev->has_flr && vdev->has_pm_reset)) {
        vbasedev->needs_reset = true;
    }
}

static Object *vfio_pci_get_object(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    return OBJECT(vdev);
}

static bool vfio_msix_present(void *opaque, int version_id)
{
    PCIDevice *pdev = opaque;

    return msix_present(pdev);
}

static bool vfio_display_migration_needed(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    /*
     * We need to migrate the VFIODisplay object if ramfb *migration* was
     * explicitly requested (in which case we enforced both ramfb=on and
     * display=on), or ramfb migration was left at the default "auto"
     * setting, and *ramfb* was explicitly requested (in which case we
     * enforced display=on).
     */
    return vdev->ramfb_migrate == ON_OFF_AUTO_ON ||
        (vdev->ramfb_migrate == ON_OFF_AUTO_AUTO && vdev->enable_ramfb);
}

static const VMStateDescription vmstate_vfio_display = {
    .name = "VFIOPCIDevice/VFIODisplay",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vfio_display_migration_needed,
    .fields = (const VMStateField[]){
        VMSTATE_STRUCT_POINTER(dpy, VFIOPCIDevice, vfio_display_vmstate,
                               VFIODisplay),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vfio_pci_config = {
    .name = "VFIOPCIDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(pdev, VFIOPCIDevice),
        VMSTATE_MSIX_TEST(pdev, VFIOPCIDevice, vfio_msix_present),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_vfio_display,
        NULL
    }
};

static int vfio_pci_save_config(VFIODevice *vbasedev, QEMUFile *f, Error **errp)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    return vmstate_save_state_with_err(f, &vmstate_vfio_pci_config, vdev, NULL,
                                       errp);
}

static int vfio_pci_load_config(VFIODevice *vbasedev, QEMUFile *f)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    PCIDevice *pdev = &vdev->pdev;
    pcibus_t old_addr[PCI_NUM_REGIONS - 1];
    int bar, ret;

    for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
        old_addr[bar] = pdev->io_regions[bar].addr;
    }

    ret = vmstate_load_state(f, &vmstate_vfio_pci_config, vdev, 1);
    if (ret) {
        return ret;
    }

    vfio_pci_write_config(pdev, PCI_COMMAND,
                          pci_get_word(pdev->config + PCI_COMMAND), 2);

    for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
        /*
         * The address may not be changed in some scenarios
         * (e.g. the VF driver isn't loaded in VM).
         */
        if (old_addr[bar] != pdev->io_regions[bar].addr &&
            vdev->bars[bar].region.size > 0 &&
            vdev->bars[bar].region.size < qemu_real_host_page_size()) {
            vfio_sub_page_bar_update_mapping(pdev, bar);
        }
    }

    if (msi_enabled(pdev)) {
        vfio_msi_enable(vdev);
    } else if (msix_enabled(pdev)) {
        vfio_msix_enable(vdev);
    }

    return ret;
}

static VFIODeviceOps vfio_pci_ops = {
    .vfio_compute_needs_reset = vfio_pci_compute_needs_reset,
    .vfio_hot_reset_multi = vfio_pci_hot_reset_multi,
    .vfio_eoi = vfio_pci_intx_eoi,
    .vfio_get_object = vfio_pci_get_object,
    .vfio_save_config = vfio_pci_save_config,
    .vfio_load_config = vfio_pci_load_config,
};

bool vfio_populate_vga(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info = NULL;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, VFIO_PCI_VGA_REGION_INDEX, &reg_info);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "failed getting region info for VGA region index %d",
                         VFIO_PCI_VGA_REGION_INDEX);
        return false;
    }

    if (!(reg_info->flags & VFIO_REGION_INFO_FLAG_READ) ||
        !(reg_info->flags & VFIO_REGION_INFO_FLAG_WRITE) ||
        reg_info->size < 0xbffff + 1) {
        error_setg(errp, "unexpected VGA info, flags 0x%lx, size 0x%lx",
                   (unsigned long)reg_info->flags,
                   (unsigned long)reg_info->size);
        return false;
    }

    vdev->vga = g_new0(VFIOVGA, 1);

    vdev->vga->fd_offset = reg_info->offset;
    vdev->vga->fd = vdev->vbasedev.fd;

    vdev->vga->region[QEMU_PCI_VGA_MEM].offset = QEMU_PCI_VGA_MEM_BASE;
    vdev->vga->region[QEMU_PCI_VGA_MEM].nr = QEMU_PCI_VGA_MEM;
    QLIST_INIT(&vdev->vga->region[QEMU_PCI_VGA_MEM].quirks);

    memory_region_init_io(&vdev->vga->region[QEMU_PCI_VGA_MEM].mem,
                          OBJECT(vdev), &vfio_vga_ops,
                          &vdev->vga->region[QEMU_PCI_VGA_MEM],
                          "vfio-vga-mmio@0xa0000",
                          QEMU_PCI_VGA_MEM_SIZE);

    vdev->vga->region[QEMU_PCI_VGA_IO_LO].offset = QEMU_PCI_VGA_IO_LO_BASE;
    vdev->vga->region[QEMU_PCI_VGA_IO_LO].nr = QEMU_PCI_VGA_IO_LO;
    QLIST_INIT(&vdev->vga->region[QEMU_PCI_VGA_IO_LO].quirks);

    memory_region_init_io(&vdev->vga->region[QEMU_PCI_VGA_IO_LO].mem,
                          OBJECT(vdev), &vfio_vga_ops,
                          &vdev->vga->region[QEMU_PCI_VGA_IO_LO],
                          "vfio-vga-io@0x3b0",
                          QEMU_PCI_VGA_IO_LO_SIZE);

    vdev->vga->region[QEMU_PCI_VGA_IO_HI].offset = QEMU_PCI_VGA_IO_HI_BASE;
    vdev->vga->region[QEMU_PCI_VGA_IO_HI].nr = QEMU_PCI_VGA_IO_HI;
    QLIST_INIT(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].quirks);

    memory_region_init_io(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                          OBJECT(vdev), &vfio_vga_ops,
                          &vdev->vga->region[QEMU_PCI_VGA_IO_HI],
                          "vfio-vga-io@0x3c0",
                          QEMU_PCI_VGA_IO_HI_SIZE);

    return true;
}

bool vfio_pci_populate_device(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info = NULL;
    struct vfio_irq_info irq_info;
    int i, ret = -1;

    /* Sanity check device */
    if (!(vbasedev->flags & VFIO_DEVICE_FLAGS_PCI)) {
        error_setg(errp, "this isn't a PCI device");
        return false;
    }

    if (vbasedev->num_regions < VFIO_PCI_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "unexpected number of io regions %u",
                   vbasedev->num_regions);
        return false;
    }

    if (vbasedev->num_irqs < VFIO_PCI_MSIX_IRQ_INDEX + 1) {
        error_setg(errp, "unexpected number of irqs %u", vbasedev->num_irqs);
        return false;
    }

    for (i = VFIO_PCI_BAR0_REGION_INDEX; i < VFIO_PCI_ROM_REGION_INDEX; i++) {
        char *name = g_strdup_printf("%s BAR %d", vbasedev->name, i);

        ret = vfio_region_setup(OBJECT(vdev), vbasedev,
                                &vdev->bars[i].region, i, name);
        g_free(name);

        if (ret) {
            error_setg_errno(errp, -ret, "failed to get region %d info", i);
            return false;
        }

        QLIST_INIT(&vdev->bars[i].quirks);
    }

    ret = vfio_device_get_region_info(vbasedev,
                                      VFIO_PCI_CONFIG_REGION_INDEX, &reg_info);
    if (ret) {
        error_setg_errno(errp, -ret, "failed to get config info");
        return false;
    }

    trace_vfio_pci_populate_device_config(vdev->vbasedev.name,
                                      (unsigned long)reg_info->size,
                                      (unsigned long)reg_info->offset,
                                      (unsigned long)reg_info->flags);

    vdev->config_size = reg_info->size;
    if (vdev->config_size == PCI_CONFIG_SPACE_SIZE) {
        vdev->pdev.cap_present &= ~QEMU_PCI_CAP_EXPRESS;
    }
    vdev->config_offset = reg_info->offset;

    if (vdev->features & VFIO_FEATURE_ENABLE_VGA) {
        if (!vfio_populate_vga(vdev, errp)) {
            error_append_hint(errp, "device does not support "
                              "requested feature x-vga\n");
            return false;
        }
    }

    ret = vfio_device_get_irq_info(vbasedev, VFIO_PCI_ERR_IRQ_INDEX, &irq_info);
    if (ret) {
        /* This can fail for an old kernel or legacy PCI dev */
        trace_vfio_pci_populate_device_get_irq_info_failure(strerror(-ret));
    } else if (irq_info.count == 1) {
        vdev->pci_aer = true;
    } else {
        warn_report(VFIO_MSG_PREFIX
                    "Could not enable error recovery for the device",
                    vbasedev->name);
    }

    return true;
}

void vfio_pci_put_device(VFIOPCIDevice *vdev)
{
    vfio_display_finalize(vdev);
    vfio_bars_finalize(vdev);
    g_free(vdev->emulated_config_bits);
    g_free(vdev->rom);
    /*
     * XXX Leaking igd_opregion is not an oversight, we can't remove the
     * fw_cfg entry therefore leaking this allocation seems like the safest
     * option.
     *
     * g_free(vdev->igd_opregion);
     */

    vfio_device_detach(&vdev->vbasedev);

    vfio_device_free_name(&vdev->vbasedev);
    g_free(vdev->msix);
}

static void vfio_err_notifier_handler(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    if (!event_notifier_test_and_clear(&vdev->err_notifier)) {
        return;
    }

    /*
     * TBD. Retrieve the error details and decide what action
     * needs to be taken. One of the actions could be to pass
     * the error to the guest and have the guest driver recover
     * from the error. This requires that PCIe capabilities be
     * exposed to the guest. For now, we just terminate the
     * guest to contain the error.
     */

    error_report("%s(%s) Unrecoverable error detected. Please collect any data possible and then kill the guest", __func__, vdev->vbasedev.name);

    vm_stop(RUN_STATE_INTERNAL_ERROR);
}

/*
 * Registers error notifier for devices supporting error recovery.
 * If we encounter a failure in this function, we report an error
 * and continue after disabling error recovery support for the
 * device.
 */
void vfio_pci_register_err_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;
    int32_t fd;

    if (!vdev->pci_aer) {
        return;
    }

    if (!vfio_notifier_init(vdev, &vdev->err_notifier, "err_notifier", 0,
                            &err)) {
        error_report_err(err);
        vdev->pci_aer = false;
        return;
    }

    fd = event_notifier_get_fd(&vdev->err_notifier);
    qemu_set_fd_handler(fd, vfio_err_notifier_handler, NULL, vdev);

    /* Do not alter irq_signaling during vfio_realize for cpr */
    if (cpr_is_incoming()) {
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_ERR_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        vfio_notifier_cleanup(vdev, &vdev->err_notifier, "err_notifier", 0);
        vdev->pci_aer = false;
    }
}

static void vfio_unregister_err_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    if (!vdev->pci_aer) {
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_ERR_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->err_notifier),
                        NULL, NULL, vdev);
    vfio_notifier_cleanup(vdev, &vdev->err_notifier, "err_notifier", 0);
}

static void vfio_req_notifier_handler(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    Error *err = NULL;

    if (!event_notifier_test_and_clear(&vdev->req_notifier)) {
        return;
    }

    qdev_unplug(DEVICE(vdev), &err);
    if (err) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
}

void vfio_pci_register_req_notifier(VFIOPCIDevice *vdev)
{
    struct vfio_irq_info irq_info;
    Error *err = NULL;
    int32_t fd;
    int ret;

    if (!(vdev->features & VFIO_FEATURE_ENABLE_REQ)) {
        return;
    }

    ret = vfio_device_get_irq_info(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX,
                                   &irq_info);
    if (ret < 0 || irq_info.count < 1) {
        return;
    }

    if (!vfio_notifier_init(vdev, &vdev->req_notifier, "req_notifier", 0,
                            &err)) {
        error_report_err(err);
        return;
    }

    fd = event_notifier_get_fd(&vdev->req_notifier);
    qemu_set_fd_handler(fd, vfio_req_notifier_handler, NULL, vdev);

    /* Do not alter irq_signaling during vfio_realize for cpr */
    if (cpr_is_incoming()) {
        vdev->req_enabled = true;
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        vfio_notifier_cleanup(vdev, &vdev->req_notifier, "req_notifier", 0);
    } else {
        vdev->req_enabled = true;
    }
}

static void vfio_unregister_req_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    if (!vdev->req_enabled) {
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->req_notifier),
                        NULL, NULL, vdev);
    vfio_notifier_cleanup(vdev, &vdev->req_notifier, "req_notifier", 0);

    vdev->req_enabled = false;
}

bool vfio_pci_config_setup(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = &vdev->pdev;
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t config_space_size;
    int ret;

    config_space_size = MIN(pci_config_size(&vdev->pdev), vdev->config_size);

    /* Get a copy of config space */
    ret = vfio_pci_config_space_read(vdev, 0, config_space_size,
                                     vdev->pdev.config);
    if (ret < (int)config_space_size) {
        ret = ret < 0 ? -ret : EFAULT;
        error_setg_errno(errp, ret, "failed to read device config space");
        return false;
    }

    /* vfio emulates a lot for us, but some bits need extra love */
    vdev->emulated_config_bits = g_malloc0(vdev->config_size);

    /* QEMU can choose to expose the ROM or not */
    memset(vdev->emulated_config_bits + PCI_ROM_ADDRESS, 0xff, 4);
    /* QEMU can also add or extend BARs */
    memset(vdev->emulated_config_bits + PCI_BASE_ADDRESS_0, 0xff, 6 * 4);

    /*
     * The PCI spec reserves vendor ID 0xffff as an invalid value.  The
     * device ID is managed by the vendor and need only be a 16-bit value.
     * Allow any 16-bit value for subsystem so they can be hidden or changed.
     */
    if (vdev->vendor_id != PCI_ANY_ID) {
        if (vdev->vendor_id >= 0xffff) {
            error_setg(errp, "invalid PCI vendor ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_VENDOR_ID, vdev->vendor_id, ~0);
        trace_vfio_pci_emulated_vendor_id(vbasedev->name, vdev->vendor_id);
    } else {
        vdev->vendor_id = pci_get_word(pdev->config + PCI_VENDOR_ID);
    }

    if (vdev->device_id != PCI_ANY_ID) {
        if (vdev->device_id > 0xffff) {
            error_setg(errp, "invalid PCI device ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_DEVICE_ID, vdev->device_id, ~0);
        trace_vfio_pci_emulated_device_id(vbasedev->name, vdev->device_id);
    } else {
        vdev->device_id = pci_get_word(pdev->config + PCI_DEVICE_ID);
    }

    if (vdev->sub_vendor_id != PCI_ANY_ID) {
        if (vdev->sub_vendor_id > 0xffff) {
            error_setg(errp, "invalid PCI subsystem vendor ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_SUBSYSTEM_VENDOR_ID,
                               vdev->sub_vendor_id, ~0);
        trace_vfio_pci_emulated_sub_vendor_id(vbasedev->name,
                                              vdev->sub_vendor_id);
    }

    if (vdev->sub_device_id != PCI_ANY_ID) {
        if (vdev->sub_device_id > 0xffff) {
            error_setg(errp, "invalid PCI subsystem device ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_SUBSYSTEM_ID, vdev->sub_device_id, ~0);
        trace_vfio_pci_emulated_sub_device_id(vbasedev->name,
                                              vdev->sub_device_id);
    }

    /*
     * Class code is a 24-bit value at config space 0x09. Allow overriding it
     * with any 24-bit value.
     */
    if (vdev->class_code != PCI_ANY_ID) {
        if (vdev->class_code > 0xffffff) {
            error_setg(errp, "invalid PCI class code provided");
            return false;
        }
        /* Higher 24 bits of PCI_CLASS_REVISION are class code */
        vfio_add_emulated_long(vdev, PCI_CLASS_REVISION,
                               vdev->class_code << 8, ~0xff);
        trace_vfio_pci_emulated_class_code(vbasedev->name, vdev->class_code);
    } else {
        vdev->class_code = pci_get_long(pdev->config + PCI_CLASS_REVISION) >> 8;
    }

    /* QEMU can change multi-function devices to single function, or reverse */
    vdev->emulated_config_bits[PCI_HEADER_TYPE] =
                                              PCI_HEADER_TYPE_MULTI_FUNCTION;

    /* Restore or clear multifunction, this is always controlled by QEMU */
    if (vdev->pdev.cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        vdev->pdev.config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
    } else {
        vdev->pdev.config[PCI_HEADER_TYPE] &= ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    }

    /*
     * Clear host resource mapping info.  If we choose not to register a
     * BAR, such as might be the case with the option ROM, we can get
     * confusing, unwritable, residual addresses from the host here.
     */
    memset(&vdev->pdev.config[PCI_BASE_ADDRESS_0], 0, 24);
    memset(&vdev->pdev.config[PCI_ROM_ADDRESS], 0, 4);

    vfio_pci_size_rom(vdev);

    vfio_bars_prepare(vdev);

    if (!vfio_msix_early_setup(vdev, errp)) {
        return false;
    }

    vfio_bars_register(vdev);

    if (vdev->vga && vfio_is_vga(vdev)) {
        pci_register_vga(&vdev->pdev, &vdev->vga->region[QEMU_PCI_VGA_MEM].mem,
                         &vdev->vga->region[QEMU_PCI_VGA_IO_LO].mem,
                         &vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem);
    }

    return true;
}

bool vfio_pci_interrupt_setup(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = &vdev->pdev;

    /* QEMU emulates all of MSI & MSIX */
    if (pdev->cap_present & QEMU_PCI_CAP_MSIX) {
        memset(vdev->emulated_config_bits + pdev->msix_cap, 0xff,
               MSIX_CAP_LENGTH);
    }

    if (pdev->cap_present & QEMU_PCI_CAP_MSI) {
        memset(vdev->emulated_config_bits + pdev->msi_cap, 0xff,
               vdev->msi_cap_size);
    }

    if (vfio_pci_read_config(&vdev->pdev, PCI_INTERRUPT_PIN, 1)) {
        vdev->intx.mmap_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                             vfio_intx_mmap_enable, vdev);
        pci_device_set_intx_routing_notifier(&vdev->pdev,
                                             vfio_intx_routing_notifier);
        vdev->irqchip_change_notifier.notify = vfio_irqchip_change;
        kvm_irqchip_add_change_notifier(&vdev->irqchip_change_notifier);

        /*
         * During CPR, do not call vfio_intx_enable at this time.  Instead,
         * call it from vfio_pci_post_load after the intx routing data has
         * been loaded from vmstate.
         */
        if (!cpr_is_incoming() && !vfio_intx_enable(vdev, errp)) {
            timer_free(vdev->intx.mmap_timer);
            pci_device_set_intx_routing_notifier(&vdev->pdev, NULL);
            kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
            return false;
        }
    }
    return true;
}

static void vfio_pci_realize(PCIDevice *pdev, Error **errp)
{
    ERRP_GUARD();
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    int i;
    char uuid[UUID_STR_LEN];
    g_autofree char *name = NULL;

    if (vbasedev->fd < 0 && !vbasedev->sysfsdev) {
        if (!(~vdev->host.domain || ~vdev->host.bus ||
              ~vdev->host.slot || ~vdev->host.function)) {
            error_setg(errp, "No provided host device");
            error_append_hint(errp, "Use -device vfio-pci,host=DDDD:BB:DD.F "
#ifdef CONFIG_IOMMUFD
                              "or -device vfio-pci,fd=DEVICE_FD "
#endif
                              "or -device vfio-pci,sysfsdev=PATH_TO_DEVICE\n");
            return;
        }
        vbasedev->sysfsdev =
            g_strdup_printf("/sys/bus/pci/devices/%04x:%02x:%02x.%01x",
                            vdev->host.domain, vdev->host.bus,
                            vdev->host.slot, vdev->host.function);
    }

    if (!vfio_device_get_name(vbasedev, errp)) {
        return;
    }

    /*
     * Mediated devices *might* operate compatibly with discarding of RAM, but
     * we cannot know for certain, it depends on whether the mdev vendor driver
     * stays in sync with the active working set of the guest driver.  Prevent
     * the x-balloon-allowed option unless this is minimally an mdev device.
     */
    vbasedev->mdev = vfio_device_is_mdev(vbasedev);

    trace_vfio_mdev(vbasedev->name, vbasedev->mdev);

    if (vbasedev->ram_block_discard_allowed && !vbasedev->mdev) {
        error_setg(errp, "x-balloon-allowed only potentially compatible "
                   "with mdev devices");
        goto error;
    }

    if (!qemu_uuid_is_null(&vdev->vf_token)) {
        qemu_uuid_unparse(&vdev->vf_token, uuid);
        name = g_strdup_printf("%s vf_token=%s", vbasedev->name, uuid);
    } else {
        name = g_strdup(vbasedev->name);
    }

    if (!vfio_device_attach(name, vbasedev,
                            pci_device_iommu_address_space(pdev), errp)) {
        goto error;
    }

    if (!vfio_pci_populate_device(vdev, errp)) {
        goto error;
    }

    if (!vfio_pci_config_setup(vdev, errp)) {
        goto error;
    }

    if (!vbasedev->mdev &&
        !pci_device_set_iommu_device(pdev, vbasedev->hiod, errp)) {
        error_prepend(errp, "Failed to set vIOMMU: ");
        goto out_teardown;
    }

    if (!vfio_pci_add_capabilities(vdev, errp)) {
        goto out_unset_idev;
    }

    if (!vfio_config_quirk_setup(vdev, errp)) {
        goto out_unset_idev;
    }

    if (vdev->vga) {
        vfio_vga_quirk_setup(vdev);
    }

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_quirk_setup(vdev, i);
    }

    if (!vfio_pci_interrupt_setup(vdev, errp)) {
        goto out_unset_idev;
    }

    if (vdev->display != ON_OFF_AUTO_OFF) {
        if (!vfio_display_probe(vdev, errp)) {
            goto out_deregister;
        }
    }
    if (vdev->enable_ramfb && vdev->dpy == NULL) {
        error_setg(errp, "ramfb=on requires display=on");
        goto out_deregister;
    }
    if (vdev->display_xres || vdev->display_yres) {
        if (vdev->dpy == NULL) {
            error_setg(errp, "xres and yres properties require display=on");
            goto out_deregister;
        }
        if (vdev->dpy->edid_regs == NULL) {
            error_setg(errp, "xres and yres properties need edid support");
            goto out_deregister;
        }
    }

    if (vdev->ramfb_migrate == ON_OFF_AUTO_ON && !vdev->enable_ramfb) {
        warn_report("x-ramfb-migrate=on but ramfb=off. "
                    "Forcing x-ramfb-migrate to off.");
        vdev->ramfb_migrate = ON_OFF_AUTO_OFF;
    }
    if (vbasedev->enable_migration == ON_OFF_AUTO_OFF) {
        if (vdev->ramfb_migrate == ON_OFF_AUTO_AUTO) {
            vdev->ramfb_migrate = ON_OFF_AUTO_OFF;
        } else if (vdev->ramfb_migrate == ON_OFF_AUTO_ON) {
            error_setg(errp, "x-ramfb-migrate requires enable-migration");
            goto out_deregister;
        }
    }

    if (!pdev->failover_pair_id) {
        if (!vfio_migration_realize(vbasedev, errp)) {
            goto out_deregister;
        }
    }

    vfio_pci_register_err_notifier(vdev);
    vfio_pci_register_req_notifier(vdev);
    vfio_setup_resetfn_quirk(vdev);

    return;

out_deregister:
    if (vdev->interrupt == VFIO_INT_INTx) {
        vfio_intx_disable(vdev);
    }
    pci_device_set_intx_routing_notifier(&vdev->pdev, NULL);
    if (vdev->irqchip_change_notifier.notify) {
        kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
    }
    if (vdev->intx.mmap_timer) {
        timer_free(vdev->intx.mmap_timer);
    }
out_unset_idev:
    if (!vbasedev->mdev) {
        pci_device_unset_iommu_device(pdev);
    }
out_teardown:
    vfio_pci_teardown_msi(vdev);
    vfio_pci_bars_exit(vdev);
error:
    error_prepend(errp, VFIO_MSG_PREFIX, vbasedev->name);
}

static void vfio_instance_finalize(Object *obj)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);

    vfio_pci_put_device(vdev);
}

static void vfio_exitfn(PCIDevice *pdev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;

    vfio_unregister_req_notifier(vdev);
    vfio_unregister_err_notifier(vdev);
    pci_device_set_intx_routing_notifier(&vdev->pdev, NULL);
    if (vdev->irqchip_change_notifier.notify) {
        kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
    }
    vfio_disable_interrupts(vdev);
    if (vdev->intx.mmap_timer) {
        timer_free(vdev->intx.mmap_timer);
    }
    vfio_pci_teardown_msi(vdev);
    vfio_pci_disable_rp_atomics(vdev);
    vfio_pci_bars_exit(vdev);
    vfio_migration_exit(vbasedev);
    if (!vbasedev->mdev) {
        pci_device_unset_iommu_device(pdev);
    }
}

static void vfio_pci_reset(DeviceState *dev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(dev);

    /* Do not reset the device during qemu_system_reset prior to cpr load */
    if (cpr_is_incoming()) {
        return;
    }

    trace_vfio_pci_reset(vdev->vbasedev.name);

    vfio_pci_pre_reset(vdev);

    if (vdev->display != ON_OFF_AUTO_OFF) {
        vfio_display_reset(vdev);
    }

    if (vdev->resetfn && !vdev->resetfn(vdev)) {
        goto post_reset;
    }

    if (vdev->vbasedev.reset_works &&
        (vdev->has_flr || !vdev->has_pm_reset) &&
        !ioctl(vdev->vbasedev.fd, VFIO_DEVICE_RESET)) {
        trace_vfio_pci_reset_flr(vdev->vbasedev.name);
        goto post_reset;
    }

    /* See if we can do our own bus reset */
    if (!vfio_pci_hot_reset_one(vdev)) {
        goto post_reset;
    }

    /* If nothing else works and the device supports PM reset, use it */
    if (vdev->vbasedev.reset_works && vdev->has_pm_reset &&
        !ioctl(vdev->vbasedev.fd, VFIO_DEVICE_RESET)) {
        trace_vfio_pci_reset_pm(vdev->vbasedev.name);
        goto post_reset;
    }

post_reset:
    vfio_pci_post_reset(vdev);
}

static void vfio_instance_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    device_add_bootindex_property(obj, &vdev->bootindex,
                                  "bootindex", NULL,
                                  &pci_dev->qdev);
    vdev->host.domain = ~0U;
    vdev->host.bus = ~0U;
    vdev->host.slot = ~0U;
    vdev->host.function = ~0U;

    vfio_device_init(vbasedev, VFIO_DEVICE_TYPE_PCI, &vfio_pci_ops,
                     DEVICE(vdev), false);

    vdev->nv_gpudirect_clique = 0xFF;

    /* QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices */
    pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;

    /*
     * A device that is resuming for cpr is already configured, so do not
     * reset it during qemu_system_reset prior to cpr load, else interrupts
     * may be lost.
     */
    pci_dev->cap_present |= QEMU_PCI_SKIP_RESET_ON_CPR;
}

static void vfio_pci_base_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    dc->desc = "VFIO PCI base device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    pdc->exit = vfio_exitfn;
    pdc->config_read = vfio_pci_read_config;
    pdc->config_write = vfio_pci_write_config;
}

static const TypeInfo vfio_pci_base_dev_info = {
    .name = TYPE_VFIO_PCI_BASE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VFIOPCIDevice),
    .abstract = true,
    .class_init = vfio_pci_base_dev_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static PropertyInfo vfio_pci_migration_multifd_transfer_prop;

static const Property vfio_pci_dev_properties[] = {
    DEFINE_PROP_PCI_HOST_DEVADDR("host", VFIOPCIDevice, host),
    DEFINE_PROP_UUID_NODEFAULT("vf-token", VFIOPCIDevice, vf_token),
    DEFINE_PROP_STRING("sysfsdev", VFIOPCIDevice, vbasedev.sysfsdev),
    DEFINE_PROP_ON_OFF_AUTO("x-pre-copy-dirty-page-tracking", VFIOPCIDevice,
                            vbasedev.pre_copy_dirty_page_tracking,
                            ON_OFF_AUTO_ON),
    DEFINE_PROP_ON_OFF_AUTO("x-device-dirty-page-tracking", VFIOPCIDevice,
                            vbasedev.device_dirty_page_tracking,
                            ON_OFF_AUTO_ON),
    DEFINE_PROP_ON_OFF_AUTO("display", VFIOPCIDevice,
                            display, ON_OFF_AUTO_OFF),
    DEFINE_PROP_UINT32("xres", VFIOPCIDevice, display_xres, 0),
    DEFINE_PROP_UINT32("yres", VFIOPCIDevice, display_yres, 0),
    DEFINE_PROP_UINT32("x-intx-mmap-timeout-ms", VFIOPCIDevice,
                       intx.mmap_timeout, 1100),
    DEFINE_PROP_BIT("x-vga", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_VGA_BIT, false),
    DEFINE_PROP_BIT("x-req", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_REQ_BIT, true),
    DEFINE_PROP_BIT("x-igd-opregion", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_IGD_OPREGION_BIT, true),
    DEFINE_PROP_BIT("x-igd-lpc", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_IGD_LPC_BIT, false),
    DEFINE_PROP_ON_OFF_AUTO("x-igd-legacy-mode", VFIOPCIDevice,
                            igd_legacy_mode, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("enable-migration", VFIOPCIDevice,
                            vbasedev.enable_migration, ON_OFF_AUTO_AUTO),
    DEFINE_PROP("x-migration-multifd-transfer", VFIOPCIDevice,
                vbasedev.migration_multifd_transfer,
                vfio_pci_migration_multifd_transfer_prop, OnOffAuto,
                .set_default = true, .defval.i = ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("x-migration-load-config-after-iter", VFIOPCIDevice,
                            vbasedev.migration_load_config_after_iter,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_SIZE("x-migration-max-queued-buffers-size", VFIOPCIDevice,
                     vbasedev.migration_max_queued_buffers_size, UINT64_MAX),
    DEFINE_PROP_BOOL("migration-events", VFIOPCIDevice,
                     vbasedev.migration_events, false),
    DEFINE_PROP_BOOL("x-no-mmap", VFIOPCIDevice, vbasedev.no_mmap, false),
    DEFINE_PROP_BOOL("x-balloon-allowed", VFIOPCIDevice,
                     vbasedev.ram_block_discard_allowed, false),
    DEFINE_PROP_BOOL("x-no-kvm-intx", VFIOPCIDevice, no_kvm_intx, false),
    DEFINE_PROP_BOOL("x-no-kvm-msi", VFIOPCIDevice, no_kvm_msi, false),
    DEFINE_PROP_BOOL("x-no-kvm-msix", VFIOPCIDevice, no_kvm_msix, false),
    DEFINE_PROP_BOOL("x-no-geforce-quirks", VFIOPCIDevice,
                     no_geforce_quirks, false),
    DEFINE_PROP_BOOL("x-no-kvm-ioeventfd", VFIOPCIDevice, no_kvm_ioeventfd,
                     false),
    DEFINE_PROP_BOOL("x-no-vfio-ioeventfd", VFIOPCIDevice, no_vfio_ioeventfd,
                     false),
    DEFINE_PROP_UINT32("x-pci-vendor-id", VFIOPCIDevice, vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-device-id", VFIOPCIDevice, device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-vendor-id", VFIOPCIDevice,
                       sub_vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-device-id", VFIOPCIDevice,
                       sub_device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-class-code", VFIOPCIDevice,
                       class_code, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-igd-gms", VFIOPCIDevice, igd_gms, 0),
    DEFINE_PROP_UNSIGNED_NODEFAULT("x-nv-gpudirect-clique", VFIOPCIDevice,
                                   nv_gpudirect_clique,
                                   qdev_prop_nv_gpudirect_clique, uint8_t),
    DEFINE_PROP_OFF_AUTO_PCIBAR("x-msix-relocation", VFIOPCIDevice, msix_relo,
                                OFF_AUTO_PCIBAR_OFF),
#ifdef CONFIG_IOMMUFD
    DEFINE_PROP_LINK("iommufd", VFIOPCIDevice, vbasedev.iommufd,
                     TYPE_IOMMUFD_BACKEND, IOMMUFDBackend *),
#endif
    DEFINE_PROP_BOOL("skip-vsc-check", VFIOPCIDevice, skip_vsc_check, true),
};

#ifdef CONFIG_IOMMUFD
static void vfio_pci_set_fd(Object *obj, const char *str, Error **errp)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);
    vfio_device_set_fd(&vdev->vbasedev, str, errp);
}
#endif

static void vfio_pci_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, vfio_pci_reset);
    device_class_set_props(dc, vfio_pci_dev_properties);
#ifdef CONFIG_IOMMUFD
    object_class_property_add_str(klass, "fd", NULL, vfio_pci_set_fd);
#endif
    dc->vmsd = &vfio_cpr_pci_vmstate;
    dc->desc = "VFIO-based PCI device assignment";
    pdc->realize = vfio_pci_realize;

    object_class_property_set_description(klass, /* 1.3 */
                                          "host",
                                          "Host PCI address [domain:]<bus:slot.function> of assigned device");
    object_class_property_set_description(klass, /* 1.3 */
                                          "x-intx-mmap-timeout-ms",
                                          "When EOI is not provided by KVM/QEMU, wait time "
                                          "(milliseconds) to re-enable device direct access "
                                          "after INTx (DEBUG)");
    object_class_property_set_description(klass, /* 1.5 */
                                          "x-vga",
                                          "Expose VGA address spaces for device");
    object_class_property_set_description(klass, /* 2.3 */
                                          "x-req",
                                          "Disable device request notification support (DEBUG)");
    object_class_property_set_description(klass, /* 2.4 and 2.5 */
                                          "x-no-mmap",
                                          "Disable MMAP for device. Allows to trace MMIO "
                                          "accesses (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-no-kvm-intx",
                                          "Disable direct VFIO->KVM INTx injection. Allows to "
                                          "trace INTx interrupts (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-no-kvm-msi",
                                          "Disable direct VFIO->KVM MSI injection. Allows to "
                                          "trace MSI interrupts (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-no-kvm-msix",
                                          "Disable direct VFIO->KVM MSIx injection. Allows to "
                                          "trace MSIx interrupts (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-vendor-id",
                                          "Override PCI Vendor ID with provided value (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-device-id",
                                          "Override PCI device ID with provided value (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-sub-vendor-id",
                                          "Override PCI Subsystem Vendor ID with provided value "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-sub-device-id",
                                          "Override PCI Subsystem Device ID with provided value "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 2.6 */
                                          "sysfsdev",
                                          "Host sysfs path of assigned device");
    object_class_property_set_description(klass, /* 2.7 */
                                          "x-igd-opregion",
                                          "Expose host IGD OpRegion to guest");
    object_class_property_set_description(klass, /* 2.7 (See c4c45e943e51) */
                                          "x-igd-gms",
                                          "Override IGD data stolen memory size (32MiB units)");
    object_class_property_set_description(klass, /* 2.11 */
                                          "x-nv-gpudirect-clique",
                                          "Add NVIDIA GPUDirect capability indicating P2P DMA "
                                          "clique for device [0-15]");
    object_class_property_set_description(klass, /* 2.12 */
                                          "x-no-geforce-quirks",
                                          "Disable GeForce quirks (for NVIDIA Quadro/GRID/Tesla). "
                                          "Improves performance");
    object_class_property_set_description(klass, /* 2.12 */
                                          "display",
                                          "Enable display support for device, ex. vGPU");
    object_class_property_set_description(klass, /* 2.12 */
                                          "x-msix-relocation",
                                          "Specify MSI-X MMIO relocation to the end of specified "
                                          "existing BAR or new BAR to avoid virtualization overhead "
                                          "due to adjacent device registers");
    object_class_property_set_description(klass, /* 3.0 */
                                          "x-no-kvm-ioeventfd",
                                          "Disable registration of ioeventfds with KVM (DEBUG)");
    object_class_property_set_description(klass, /* 3.0 */
                                          "x-no-vfio-ioeventfd",
                                          "Disable linking of KVM ioeventfds to VFIO ioeventfds "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 3.1 */
                                          "x-balloon-allowed",
                                          "Override allowing ballooning with device (DEBUG, DANGER)");
    object_class_property_set_description(klass, /* 3.2 */
                                          "xres",
                                          "Set X display resolution the vGPU should use");
    object_class_property_set_description(klass, /* 3.2 */
                                          "yres",
                                          "Set Y display resolution the vGPU should use");
    object_class_property_set_description(klass, /* 5.2 */
                                          "x-pre-copy-dirty-page-tracking",
                                          "Disable dirty pages tracking during iterative phase "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 5.2, 8.0 non-experimetal */
                                          "enable-migration",
                                          "Enale device migration. Also requires a host VFIO PCI "
                                          "variant or mdev driver with migration support enabled");
    object_class_property_set_description(klass, /* 8.1 */
                                          "vf-token",
                                          "Specify UUID VF token. Required for VF when PF is owned "
                                          "by another VFIO driver");
#ifdef CONFIG_IOMMUFD
    object_class_property_set_description(klass, /* 9.0 */
                                          "iommufd",
                                          "Set host IOMMUFD backend device");
#endif
    object_class_property_set_description(klass, /* 9.1 */
                                          "x-device-dirty-page-tracking",
                                          "Disable device dirty page tracking and use "
                                          "container-based dirty page tracking");
    object_class_property_set_description(klass, /* 9.1 */
                                          "migration-events",
                                          "Emit VFIO migration QAPI event when a VFIO device "
                                          "changes its migration state. For management applications");
    object_class_property_set_description(klass, /* 9.1 */
                                          "skip-vsc-check",
                                          "Skip config space check for Vendor Specific Capability. "
                                          "Setting to false will enforce strict checking of VSC content "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 10.0 */
                                          "x-migration-multifd-transfer",
                                          "Transfer this device state via "
                                          "multifd channels when live migrating it");
    object_class_property_set_description(klass, /* 10.1 */
                                          "x-migration-load-config-after-iter",
                                          "Start the config load only after "
                                          "all iterables were loaded (during "
                                          "non-iterables loading phase) when "
                                          "doing live migration of device state "
                                          "via multifd channels");
    object_class_property_set_description(klass, /* 10.1 */
                                          "x-migration-max-queued-buffers-size",
                                          "Maximum size of in-flight VFIO "
                                          "device state buffers queued at the "
                                          "destination when doing live "
                                          "migration of device state via "
                                          "multifd channels");
}

static const TypeInfo vfio_pci_dev_info = {
    .name = TYPE_VFIO_PCI,
    .parent = TYPE_VFIO_PCI_BASE,
    .class_init = vfio_pci_dev_class_init,
    .instance_init = vfio_instance_init,
    .instance_finalize = vfio_instance_finalize,
};

static const Property vfio_pci_dev_nohotplug_properties[] = {
    DEFINE_PROP_BOOL("ramfb", VFIOPCIDevice, enable_ramfb, false),
    DEFINE_PROP_ON_OFF_AUTO("x-ramfb-migrate", VFIOPCIDevice, ramfb_migrate,
                            ON_OFF_AUTO_AUTO),
};

static void vfio_pci_nohotplug_dev_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_pci_dev_nohotplug_properties);
    dc->hotpluggable = false;

    object_class_property_set_description(klass, /* 3.1 */
                                          "ramfb",
                                          "Enable ramfb to provide pre-boot graphics for devices "
                                          "enabling display option");
    object_class_property_set_description(klass, /* 8.2 */
                                          "x-ramfb-migrate",
                                          "Override default migration support for ramfb support "
                                          "(DEBUG)");
}

static const TypeInfo vfio_pci_nohotplug_dev_info = {
    .name = TYPE_VFIO_PCI_NOHOTPLUG,
    .parent = TYPE_VFIO_PCI,
    .instance_size = sizeof(VFIOPCIDevice),
    .class_init = vfio_pci_nohotplug_dev_class_init,
};

static void register_vfio_pci_dev_type(void)
{
    /*
     * Ordinary ON_OFF_AUTO property isn't runtime-mutable, but source VM can
     * run for a long time before being migrated so it is desirable to have a
     * fallback mechanism to the old way of transferring VFIO device state if
     * it turns to be necessary.
     * The following makes this type of property have the same mutability level
     * as ordinary migration parameters.
     */
    vfio_pci_migration_multifd_transfer_prop = qdev_prop_on_off_auto;
    vfio_pci_migration_multifd_transfer_prop.realized_set_allowed = true;

    type_register_static(&vfio_pci_base_dev_info);
    type_register_static(&vfio_pci_dev_info);
    type_register_static(&vfio_pci_nohotplug_dev_info);
}

type_init(register_vfio_pci_dev_type)
