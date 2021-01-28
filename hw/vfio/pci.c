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
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_bridge.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "pci.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/blocker.h"
#include "migration/qemu-file.h"

#define TYPE_VFIO_PCI_NOHOTPLUG "vfio-pci-nohotplug"

static void vfio_disable_interrupts(VFIOPCIDevice *vdev);
static void vfio_mmap_set_enabled(VFIOPCIDevice *vdev, bool enabled);

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

static void vfio_intx_eoi(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    if (!vdev->intx.pending) {
        return;
    }

    trace_vfio_intx_eoi(vbasedev->name);

    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);
    vfio_unmask_single_irqindex(vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
}

static void vfio_intx_enable_kvm(VFIOPCIDevice *vdev, Error **errp)
{
#ifdef CONFIG_KVM
    int irq_fd = event_notifier_get_fd(&vdev->intx.interrupt);

    if (vdev->no_kvm_intx || !kvm_irqfds_enabled() ||
        vdev->intx.route.mode != PCI_INTX_ENABLED ||
        !kvm_resamplefds_enabled()) {
        return;
    }

    /* Get to a known interrupt state */
    qemu_set_fd_handler(irq_fd, NULL, NULL, vdev);
    vfio_mask_single_irqindex(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);

    /* Get an eventfd for resample/unmask */
    if (event_notifier_init(&vdev->intx.unmask, 0)) {
        error_setg(errp, "event_notifier_init failed eoi");
        goto fail;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &vdev->intx.interrupt,
                                           &vdev->intx.unmask,
                                           vdev->intx.route.irq)) {
        error_setg_errno(errp, errno, "failed to setup resample irqfd");
        goto fail_irqfd;
    }

    if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_UNMASK,
                               event_notifier_get_fd(&vdev->intx.unmask),
                               errp)) {
        goto fail_vfio;
    }

    /* Let'em rip */
    vfio_unmask_single_irqindex(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);

    vdev->intx.kvm_accel = true;

    trace_vfio_intx_enable_kvm(vdev->vbasedev.name);

    return;

fail_vfio:
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vdev->intx.interrupt,
                                          vdev->intx.route.irq);
fail_irqfd:
    event_notifier_cleanup(&vdev->intx.unmask);
fail:
    qemu_set_fd_handler(irq_fd, vfio_intx_interrupt, NULL, vdev);
    vfio_unmask_single_irqindex(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
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
    vfio_mask_single_irqindex(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);

    /* Tell KVM to stop listening for an INTx irqfd */
    if (kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vdev->intx.interrupt,
                                              vdev->intx.route.irq)) {
        error_report("vfio: Error: Failed to disable INTx irqfd: %m");
    }

    /* We only need to close the eventfd for VFIO to cleanup the kernel side */
    event_notifier_cleanup(&vdev->intx.unmask);

    /* QEMU starts listening for interrupt events. */
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->intx.interrupt),
                        vfio_intx_interrupt, NULL, vdev);

    vdev->intx.kvm_accel = false;

    /* If we've missed an event, let it re-fire through QEMU */
    vfio_unmask_single_irqindex(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);

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

    vfio_intx_enable_kvm(vdev, &err);
    if (err) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    /* Re-enable the interrupt in cased we missed an EOI */
    vfio_intx_eoi(&vdev->vbasedev);
}

static void vfio_intx_routing_notifier(PCIDevice *pdev)
{
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
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

static int vfio_intx_enable(VFIOPCIDevice *vdev, Error **errp)
{
    uint8_t pin = vfio_pci_read_config(&vdev->pdev, PCI_INTERRUPT_PIN, 1);
    Error *err = NULL;
    int32_t fd;
    int ret;


    if (!pin) {
        return 0;
    }

    vfio_disable_interrupts(vdev);

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

    ret = event_notifier_init(&vdev->intx.interrupt, 0);
    if (ret) {
        error_setg_errno(errp, -ret, "event_notifier_init failed");
        return ret;
    }
    fd = event_notifier_get_fd(&vdev->intx.interrupt);
    qemu_set_fd_handler(fd, vfio_intx_interrupt, NULL, vdev);

    if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_TRIGGER, fd, errp)) {
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        event_notifier_cleanup(&vdev->intx.interrupt);
        return -errno;
    }

    vfio_intx_enable_kvm(vdev, &err);
    if (err) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    vdev->interrupt = VFIO_INT_INTx;

    trace_vfio_intx_enable(vdev->vbasedev.name);
    return 0;
}

static void vfio_intx_disable(VFIOPCIDevice *vdev)
{
    int fd;

    timer_del(vdev->intx.mmap_timer);
    vfio_intx_disable_kvm(vdev);
    vfio_disable_irqindex(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(&vdev->pdev);
    vfio_mmap_set_enabled(vdev, true);

    fd = event_notifier_get_fd(&vdev->intx.interrupt);
    qemu_set_fd_handler(fd, NULL, NULL, vdev);
    event_notifier_cleanup(&vdev->intx.interrupt);

    vdev->interrupt = VFIO_INT_NONE;

    trace_vfio_intx_disable(vdev->vbasedev.name);
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

static int vfio_enable_vectors(VFIOPCIDevice *vdev, bool msix)
{
    struct vfio_irq_set *irq_set;
    int ret = 0, i, argsz;
    int32_t *fds;

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

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_SET_IRQS, irq_set);

    g_free(irq_set);

    return ret;
}

static void vfio_add_kvm_msi_virq(VFIOPCIDevice *vdev, VFIOMSIVector *vector,
                                  int vector_n, bool msix)
{
    int virq;

    if ((msix && vdev->no_kvm_msix) || (!msix && vdev->no_kvm_msi)) {
        return;
    }

    if (event_notifier_init(&vector->kvm_interrupt, 0)) {
        return;
    }

    virq = kvm_irqchip_add_msi_route(kvm_state, vector_n, &vdev->pdev);
    if (virq < 0) {
        event_notifier_cleanup(&vector->kvm_interrupt);
        return;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, &vector->kvm_interrupt,
                                       NULL, virq) < 0) {
        kvm_irqchip_release_virq(kvm_state, virq);
        event_notifier_cleanup(&vector->kvm_interrupt);
        return;
    }

    vector->virq = virq;
}

static void vfio_remove_kvm_msi_virq(VFIOMSIVector *vector)
{
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vector->kvm_interrupt,
                                          vector->virq);
    kvm_irqchip_release_virq(kvm_state, vector->virq);
    vector->virq = -1;
    event_notifier_cleanup(&vector->kvm_interrupt);
}

static void vfio_update_kvm_msi_virq(VFIOMSIVector *vector, MSIMessage msg,
                                     PCIDevice *pdev)
{
    kvm_irqchip_update_msi_route(kvm_state, vector->virq, msg, pdev);
    kvm_irqchip_commit_routes(kvm_state);
}

static int vfio_msix_vector_do_use(PCIDevice *pdev, unsigned int nr,
                                   MSIMessage *msg, IOHandler *handler)
{
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
    VFIOMSIVector *vector;
    int ret;

    trace_vfio_msix_vector_do_use(vdev->vbasedev.name, nr);

    vector = &vdev->msi_vectors[nr];

    if (!vector->use) {
        vector->vdev = vdev;
        vector->virq = -1;
        if (event_notifier_init(&vector->interrupt, 0)) {
            error_report("vfio: Error: event_notifier_init failed");
        }
        vector->use = true;
        msix_vector_use(pdev, nr);
    }

    qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                        handler, NULL, vector);

    /*
     * Attempt to enable route through KVM irqchip,
     * default to userspace handling if unavailable.
     */
    if (vector->virq >= 0) {
        if (!msg) {
            vfio_remove_kvm_msi_virq(vector);
        } else {
            vfio_update_kvm_msi_virq(vector, *msg, pdev);
        }
    } else {
        if (msg) {
            vfio_add_kvm_msi_virq(vdev, vector, nr, true);
        }
    }

    /*
     * We don't want to have the host allocate all possible MSI vectors
     * for a device if they're not in use, so we shutdown and incrementally
     * increase them as needed.
     */
    if (vdev->nr_vectors < nr + 1) {
        vfio_disable_irqindex(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX);
        vdev->nr_vectors = nr + 1;
        ret = vfio_enable_vectors(vdev, true);
        if (ret) {
            error_report("vfio: failed to enable vectors, %d", ret);
        }
    } else {
        Error *err = NULL;
        int32_t fd;

        if (vector->virq >= 0) {
            fd = event_notifier_get_fd(&vector->kvm_interrupt);
        } else {
            fd = event_notifier_get_fd(&vector->interrupt);
        }

        if (vfio_set_irq_signaling(&vdev->vbasedev,
                                     VFIO_PCI_MSIX_IRQ_INDEX, nr,
                                     VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
            error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
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
    return vfio_msix_vector_do_use(pdev, nr, &msg, vfio_msi_interrupt);
}

static void vfio_msix_vector_release(PCIDevice *pdev, unsigned int nr)
{
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
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

        if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX, nr,
                                   VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
            error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        }
    }
}

static void vfio_msix_enable(VFIOPCIDevice *vdev)
{
    vfio_disable_interrupts(vdev);

    vdev->msi_vectors = g_new0(VFIOMSIVector, vdev->msix->entries);

    vdev->interrupt = VFIO_INT_MSIX;

    /*
     * Some communication channels between VF & PF or PF & fw rely on the
     * physical state of the device and expect that enabling MSI-X from the
     * guest enables the same on the host.  When our guest is Linux, the
     * guest driver call to pci_enable_msix() sets the enabling bit in the
     * MSI-X capability, but leaves the vector table masked.  We therefore
     * can't rely on a vector_use callback (from request_irq() in the guest)
     * to switch the physical device into MSI-X mode because that may come a
     * long time after pci_enable_msix().  This code enables vector 0 with
     * triggering to userspace, then immediately release the vector, leaving
     * the physical device with no vectors enabled, but MSI-X enabled, just
     * like the guest view.
     */
    vfio_msix_vector_do_use(&vdev->pdev, 0, NULL, NULL);
    vfio_msix_vector_release(&vdev->pdev, 0);

    if (msix_set_vector_notifiers(&vdev->pdev, vfio_msix_vector_use,
                                  vfio_msix_vector_release, NULL)) {
        error_report("vfio: msix_set_vector_notifiers failed");
    }

    trace_vfio_msix_enable(vdev->vbasedev.name);
}

static void vfio_msi_enable(VFIOPCIDevice *vdev)
{
    int ret, i;

    vfio_disable_interrupts(vdev);

    vdev->nr_vectors = msi_nr_vectors_allocated(&vdev->pdev);
retry:
    vdev->msi_vectors = g_new0(VFIOMSIVector, vdev->nr_vectors);

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];

        vector->vdev = vdev;
        vector->virq = -1;
        vector->use = true;

        if (event_notifier_init(&vector->interrupt, 0)) {
            error_report("vfio: Error: event_notifier_init failed");
        }

        qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                            vfio_msi_interrupt, NULL, vector);

        /*
         * Attempt to enable route through KVM irqchip,
         * default to userspace handling if unavailable.
         */
        vfio_add_kvm_msi_virq(vdev, vector, i, false);
    }

    /* Set interrupt type prior to possible interrupts */
    vdev->interrupt = VFIO_INT_MSI;

    ret = vfio_enable_vectors(vdev, false);
    if (ret) {
        if (ret < 0) {
            error_report("vfio: Error: Failed to setup MSI fds: %m");
        } else if (ret != vdev->nr_vectors) {
            error_report("vfio: Error: Failed to enable %d "
                         "MSI vectors, retry with %d", vdev->nr_vectors, ret);
        }

        for (i = 0; i < vdev->nr_vectors; i++) {
            VFIOMSIVector *vector = &vdev->msi_vectors[i];
            if (vector->virq >= 0) {
                vfio_remove_kvm_msi_virq(vector);
            }
            qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                                NULL, NULL, NULL);
            event_notifier_cleanup(&vector->interrupt);
        }

        g_free(vdev->msi_vectors);
        vdev->msi_vectors = NULL;

        if (ret > 0 && ret != vdev->nr_vectors) {
            vdev->nr_vectors = ret;
            goto retry;
        }
        vdev->nr_vectors = 0;

        /*
         * Failing to setup MSI doesn't really fall within any specification.
         * Let's try leaving interrupts disabled and hope the guest figures
         * out to fall back to INTx for this device.
         */
        error_report("vfio: Error: Failed to enable MSI");
        vdev->interrupt = VFIO_INT_NONE;

        return;
    }

    trace_vfio_msi_enable(vdev->vbasedev.name, vdev->nr_vectors);
}

static void vfio_msi_disable_common(VFIOPCIDevice *vdev)
{
    Error *err = NULL;
    int i;

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        if (vdev->msi_vectors[i].use) {
            if (vector->virq >= 0) {
                vfio_remove_kvm_msi_virq(vector);
            }
            qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                                NULL, NULL, NULL);
            event_notifier_cleanup(&vector->interrupt);
        }
    }

    g_free(vdev->msi_vectors);
    vdev->msi_vectors = NULL;
    vdev->nr_vectors = 0;
    vdev->interrupt = VFIO_INT_NONE;

    vfio_intx_enable(vdev, &err);
    if (err) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
}

static void vfio_msix_disable(VFIOPCIDevice *vdev)
{
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

    if (vdev->nr_vectors) {
        vfio_disable_irqindex(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX);
    }

    vfio_msi_disable_common(vdev);

    memset(vdev->msix->pending, 0,
           BITS_TO_LONGS(vdev->msix->entries) * sizeof(unsigned long));

    trace_vfio_msix_disable(vdev->vbasedev.name);
}

static void vfio_msi_disable(VFIOPCIDevice *vdev)
{
    vfio_disable_irqindex(&vdev->vbasedev, VFIO_PCI_MSI_IRQ_INDEX);
    vfio_msi_disable_common(vdev);

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
    struct vfio_region_info *reg_info;
    uint64_t size;
    off_t off = 0;
    ssize_t bytes;

    if (vfio_get_region_info(&vdev->vbasedev,
                             VFIO_PCI_ROM_REGION_INDEX, &reg_info)) {
        error_report("vfio: Error getting ROM info: %m");
        return;
    }

    trace_vfio_pci_load_rom(vdev->vbasedev.name, (unsigned long)reg_info->size,
                            (unsigned long)reg_info->offset,
                            (unsigned long)reg_info->flags);

    vdev->rom_size = size = reg_info->size;
    vdev->rom_offset = reg_info->offset;

    g_free(reg_info);

    if (!vdev->rom_size) {
        vdev->rom_read_failed = true;
        error_report("vfio-pci: Cannot read device rom at "
                    "%s", vdev->vbasedev.name);
        error_printf("Device option ROM contents are probably invalid "
                    "(check dmesg).\nSkip option ROM probe with rombar=0, "
                    "or load from file with romfile=\n");
        return;
    }

    vdev->rom = g_malloc(size);
    memset(vdev->rom, 0xff, size);

    while (size) {
        bytes = pread(vdev->vbasedev.fd, vdev->rom + off,
                      size, vdev->rom_offset + off);
        if (bytes == 0) {
            break;
        } else if (bytes > 0) {
            off += bytes;
            size -= bytes;
        } else {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            error_report("vfio: Error reading device ROM: %m");
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
    uint32_t orig, size = cpu_to_le32((uint32_t)PCI_ROM_ADDRESS_MASK);
    off_t offset = vdev->config_offset + PCI_ROM_ADDRESS;
    DeviceState *dev = DEVICE(vdev);
    char *name;
    int fd = vdev->vbasedev.fd;

    if (vdev->pdev.romfile || !vdev->pdev.rom_bar) {
        /* Since pci handles romfile, just print a message and return */
        if (vfio_blacklist_opt_rom(vdev) && vdev->pdev.romfile) {
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
    if (pread(fd, &orig, 4, offset) != 4 ||
        pwrite(fd, &size, 4, offset) != 4 ||
        pread(fd, &size, 4, offset) != 4 ||
        pwrite(fd, &orig, 4, offset) != 4) {
        error_report("%s(%s) failed: %m", __func__, vdev->vbasedev.name);
        return;
    }

    size = ~(le32_to_cpu(size) & PCI_ROM_ADDRESS_MASK) + 1;

    if (!size) {
        return;
    }

    if (vfio_blacklist_opt_rom(vdev)) {
        if (dev->opts && qemu_opt_get(dev->opts, "rombar")) {
            warn_report("Device at %s is known to cause system instability"
                        " issues during option rom execution",
                        vdev->vbasedev.name);
            error_printf("Proceeding anyway since user specified"
                         " non zero value for rombar\n");
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
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
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
        !(bar_addr & ~qemu_real_host_page_mask)) {
        size = qemu_real_host_page_size;
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
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
    uint32_t emu_bits = 0, emu_val = 0, phys_val = 0, val;

    memcpy(&emu_bits, vdev->emulated_config_bits + addr, len);
    emu_bits = le32_to_cpu(emu_bits);

    if (emu_bits) {
        emu_val = pci_default_read_config(pdev, addr, len);
    }

    if (~emu_bits & (0xffffffffU >> (32 - len * 8))) {
        ssize_t ret;

        ret = pread(vdev->vbasedev.fd, &phys_val, len,
                    vdev->config_offset + addr);
        if (ret != len) {
            error_report("%s(%s, 0x%x, 0x%x) failed: %m",
                         __func__, vdev->vbasedev.name, addr, len);
            return -errno;
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
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
    uint32_t val_le = cpu_to_le32(val);

    trace_vfio_pci_write_config(vdev->vbasedev.name, addr, val, len);

    /* Write everything to VFIO, let it filter out what we can't write */
    if (pwrite(vdev->vbasedev.fd, &val_le, len, vdev->config_offset + addr)
                != len) {
        error_report("%s(%s, 0x%x, 0x%x, 0x%x) failed: %m",
                     __func__, vdev->vbasedev.name, addr, val, len);
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
                vdev->bars[bar].region.size < qemu_real_host_page_size) {
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

static int vfio_msi_setup(VFIOPCIDevice *vdev, int pos, Error **errp)
{
    uint16_t ctrl;
    bool msi_64bit, msi_maskbit;
    int ret, entries;
    Error *err = NULL;

    if (pread(vdev->vbasedev.fd, &ctrl, sizeof(ctrl),
              vdev->config_offset + pos + PCI_CAP_FLAGS) != sizeof(ctrl)) {
        error_setg_errno(errp, errno, "failed reading MSI PCI_CAP_FLAGS");
        return -errno;
    }
    ctrl = le16_to_cpu(ctrl);

    msi_64bit = !!(ctrl & PCI_MSI_FLAGS_64BIT);
    msi_maskbit = !!(ctrl & PCI_MSI_FLAGS_MASKBIT);
    entries = 1 << ((ctrl & PCI_MSI_FLAGS_QMASK) >> 1);

    trace_vfio_msi_setup(vdev->vbasedev.name, pos);

    ret = msi_init(&vdev->pdev, pos, entries, msi_64bit, msi_maskbit, &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            return 0;
        }
        error_propagate_prepend(errp, err, "msi_init failed: ");
        return ret;
    }
    vdev->msi_cap_size = 0xa + (msi_maskbit ? 0xa : 0) + (msi_64bit ? 0x4 : 0);

    return 0;
}

static void vfio_pci_fixup_msix_region(VFIOPCIDevice *vdev)
{
    off_t start, end;
    VFIORegion *region = &vdev->bars[vdev->msix->table_bar].region;

    /*
     * If the host driver allows mapping of a MSIX data, we are going to
     * do map the entire BAR and emulate MSIX table on top of that.
     */
    if (vfio_has_region_cap(&vdev->vbasedev, region->nr,
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
    start = vdev->msix->table_offset & qemu_real_host_page_mask;
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

static void vfio_pci_relocate_msix(VFIOPCIDevice *vdev, Error **errp)
{
    int target_bar = -1;
    size_t msix_sz;

    if (!vdev->msix || vdev->msix_relo == OFF_AUTOPCIBAR_OFF) {
        return;
    }

    /* The actual minimum size of MSI-X structures */
    msix_sz = (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE) +
              (QEMU_ALIGN_UP(vdev->msix->entries, 64) / 8);
    /* Round up to host pages, we don't want to share a page */
    msix_sz = REAL_HOST_PAGE_ALIGN(msix_sz);
    /* PCI BARs must be a power of 2 */
    msix_sz = pow2ceil(msix_sz);

    if (vdev->msix_relo == OFF_AUTOPCIBAR_AUTO) {
        /*
         * TODO: Lookup table for known devices.
         *
         * Logically we might use an algorithm here to select the BAR adding
         * the least additional MMIO space, but we cannot programatically
         * predict the driver dependency on BAR ordering or sizing, therefore
         * 'auto' becomes a lookup for combinations reported to work.
         */
        if (target_bar < 0) {
            error_setg(errp, "No automatic MSI-X relocation available for "
                       "device %04x:%04x", vdev->vendor_id, vdev->device_id);
            return;
        }
    } else {
        target_bar = (int)(vdev->msix_relo - OFF_AUTOPCIBAR_BAR0);
    }

    /* I/O port BARs cannot host MSI-X structures */
    if (vdev->bars[target_bar].ioport) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "I/O port BAR", target_bar);
        return;
    }

    /* Cannot use a BAR in the "shadow" of a 64-bit BAR */
    if (!vdev->bars[target_bar].size &&
         target_bar > 0 && vdev->bars[target_bar - 1].mem64) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "consumed by 64-bit BAR %d", target_bar, target_bar - 1);
        return;
    }

    /* 2GB max size for 32-bit BARs, cannot double if already > 1G */
    if (vdev->bars[target_bar].size > 1 * GiB &&
        !vdev->bars[target_bar].mem64) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "no space to extend 32-bit BAR", target_bar);
        return;
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
}

/*
 * We don't have any control over how pci_add_capability() inserts
 * capabilities into the chain.  In order to setup MSI-X we need a
 * MemoryRegion for the BAR.  In order to setup the BAR and not
 * attempt to mmap the MSI-X table area, which VFIO won't allow, we
 * need to first look for where the MSI-X table lives.  So we
 * unfortunately split MSI-X setup across two functions.
 */
static void vfio_msix_early_setup(VFIOPCIDevice *vdev, Error **errp)
{
    uint8_t pos;
    uint16_t ctrl;
    uint32_t table, pba;
    int fd = vdev->vbasedev.fd;
    VFIOMSIXInfo *msix;

    pos = pci_find_capability(&vdev->pdev, PCI_CAP_ID_MSIX);
    if (!pos) {
        return;
    }

    if (pread(fd, &ctrl, sizeof(ctrl),
              vdev->config_offset + pos + PCI_MSIX_FLAGS) != sizeof(ctrl)) {
        error_setg_errno(errp, errno, "failed to read PCI MSIX FLAGS");
        return;
    }

    if (pread(fd, &table, sizeof(table),
              vdev->config_offset + pos + PCI_MSIX_TABLE) != sizeof(table)) {
        error_setg_errno(errp, errno, "failed to read PCI MSIX TABLE");
        return;
    }

    if (pread(fd, &pba, sizeof(pba),
              vdev->config_offset + pos + PCI_MSIX_PBA) != sizeof(pba)) {
        error_setg_errno(errp, errno, "failed to read PCI MSIX PBA");
        return;
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
        } else if (vdev->msix_relo == OFF_AUTOPCIBAR_OFF) {
            error_setg(errp, "hardware reports invalid configuration, "
                       "MSIX PBA outside of specified BAR");
            g_free(msix);
            return;
        }
    }

    trace_vfio_msix_early_setup(vdev->vbasedev.name, pos, msix->table_bar,
                                msix->table_offset, msix->entries);
    vdev->msix = msix;

    vfio_pci_fixup_msix_region(vdev);

    vfio_pci_relocate_msix(vdev, errp);
}

static int vfio_msix_setup(VFIOPCIDevice *vdev, int pos, Error **errp)
{
    int ret;
    Error *err = NULL;

    vdev->msix->pending = g_malloc0(BITS_TO_LONGS(vdev->msix->entries) *
                                    sizeof(unsigned long));
    ret = msix_init(&vdev->pdev, vdev->msix->entries,
                    vdev->bars[vdev->msix->table_bar].mr,
                    vdev->msix->table_bar, vdev->msix->table_offset,
                    vdev->bars[vdev->msix->pba_bar].mr,
                    vdev->msix->pba_bar, vdev->msix->pba_offset, pos,
                    &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            warn_report_err(err);
            return 0;
        }

        error_propagate(errp, err);
        return ret;
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

    return 0;
}

static void vfio_teardown_msi(VFIOPCIDevice *vdev)
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
    ret = pread(vdev->vbasedev.fd, &pci_bar, sizeof(pci_bar),
                vdev->config_offset + PCI_BASE_ADDRESS_0 + (4 * nr));
    if (ret != sizeof(pci_bar)) {
        error_report("vfio: Failed to read BAR %d (%m)", nr);
        return;
    }

    pci_bar = le32_to_cpu(pci_bar);
    bar->ioport = (pci_bar & PCI_BASE_ADDRESS_SPACE_IO);
    bar->mem64 = bar->ioport ? 0 : (pci_bar & PCI_BASE_ADDRESS_MEM_TYPE_64);
    bar->type = pci_bar & (bar->ioport ? ~PCI_BASE_ADDRESS_IO_MASK :
                                         ~PCI_BASE_ADDRESS_MEM_MASK);
    bar->size = bar->region.size;
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

static void vfio_bars_exit(VFIOPCIDevice *vdev)
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
        if (bar->size) {
            object_unparent(OBJECT(bar->mr));
            g_free(bar->mr);
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

static int vfio_setup_pcie_cap(VFIOPCIDevice *vdev, int pos, uint8_t size,
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
        return -EINVAL;
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
            return 0;
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
            return 0;
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
        return pos;
    }

    vdev->pdev.exp.exp_cap = pos;

    return pos;
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

static int vfio_add_std_cap(VFIOPCIDevice *vdev, uint8_t pos, Error **errp)
{
    PCIDevice *pdev = &vdev->pdev;
    uint8_t cap_id, next, size;
    int ret;

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
        ret = vfio_add_std_cap(vdev, next, errp);
        if (ret) {
            return ret;
        }
    } else {
        /* Begin the rebuild, use QEMU emulated list bits */
        pdev->config[PCI_CAPABILITY_LIST] = 0;
        vdev->emulated_config_bits[PCI_CAPABILITY_LIST] = 0xff;
        vdev->emulated_config_bits[PCI_STATUS] |= PCI_STATUS_CAP_LIST;

        ret = vfio_add_virt_caps(vdev, errp);
        if (ret) {
            return ret;
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
        vdev->pm_cap = pos;
        ret = pci_add_capability(pdev, cap_id, pos, size, errp);
        break;
    case PCI_CAP_ID_AF:
        vfio_check_af_flr(vdev, pos);
        ret = pci_add_capability(pdev, cap_id, pos, size, errp);
        break;
    default:
        ret = pci_add_capability(pdev, cap_id, pos, size, errp);
        break;
    }

    if (ret < 0) {
        error_prepend(errp,
                      "failed to add PCI capability 0x%x[0x%x]@0x%x: ",
                      cap_id, size, pos);
        return ret;
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
        case PCI_EXT_CAP_ID_REBAR: /* Can't expose read-only */
            trace_vfio_add_ext_cap_dropped(vdev->vbasedev.name, cap_id, next);
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
    return;
}

static int vfio_add_capabilities(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = &vdev->pdev;
    int ret;

    if (!(pdev->config[PCI_STATUS] & PCI_STATUS_CAP_LIST) ||
        !pdev->config[PCI_CAPABILITY_LIST]) {
        return 0; /* Nothing to add */
    }

    ret = vfio_add_std_cap(vdev, pdev->config[PCI_CAPABILITY_LIST], errp);
    if (ret) {
        return ret;
    }

    vfio_add_ext_cap(vdev);
    return 0;
}

static void vfio_pci_pre_reset(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = &vdev->pdev;
    uint16_t cmd;

    vfio_disable_interrupts(vdev);

    /* Make sure the device is in D0 */
    if (vdev->pm_cap) {
        uint16_t pmcsr;
        uint8_t state;

        pmcsr = vfio_pci_read_config(pdev, vdev->pm_cap + PCI_PM_CTRL, 2);
        state = pmcsr & PCI_PM_CTRL_STATE_MASK;
        if (state) {
            pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
            vfio_pci_write_config(pdev, vdev->pm_cap + PCI_PM_CTRL, pmcsr, 2);
            /* vfio handles the necessary delay here */
            pmcsr = vfio_pci_read_config(pdev, vdev->pm_cap + PCI_PM_CTRL, 2);
            state = pmcsr & PCI_PM_CTRL_STATE_MASK;
            if (state) {
                error_report("vfio: Unable to power on device, stuck in D%d",
                             state);
            }
        }
    }

    /*
     * Stop any ongoing DMA by disconecting I/O, MMIO, and bus master.
     * Also put INTx Disable in known state.
     */
    cmd = vfio_pci_read_config(pdev, PCI_COMMAND, 2);
    cmd &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
             PCI_COMMAND_INTX_DISABLE);
    vfio_pci_write_config(pdev, PCI_COMMAND, cmd, 2);
}

static void vfio_pci_post_reset(VFIOPCIDevice *vdev)
{
    Error *err = NULL;
    int nr;

    vfio_intx_enable(vdev, &err);
    if (err) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    for (nr = 0; nr < PCI_NUM_REGIONS - 1; ++nr) {
        off_t addr = vdev->config_offset + PCI_BASE_ADDRESS_0 + (4 * nr);
        uint32_t val = 0;
        uint32_t len = sizeof(val);

        if (pwrite(vdev->vbasedev.fd, &val, len, addr) != len) {
            error_report("%s(%s) reset bar %d failed: %m", __func__,
                         vdev->vbasedev.name, nr);
        }
    }

    vfio_quirk_reset(vdev);
}

static bool vfio_pci_host_match(PCIHostDeviceAddress *addr, const char *name)
{
    char tmp[13];

    sprintf(tmp, "%04x:%02x:%02x.%1x", addr->domain,
            addr->bus, addr->slot, addr->function);

    return (strcmp(tmp, name) == 0);
}

static int vfio_pci_hot_reset(VFIOPCIDevice *vdev, bool single)
{
    VFIOGroup *group;
    struct vfio_pci_hot_reset_info *info;
    struct vfio_pci_dependent_device *devices;
    struct vfio_pci_hot_reset *reset;
    int32_t *fds;
    int ret, i, count;
    bool multi = false;

    trace_vfio_pci_hot_reset(vdev->vbasedev.name, single ? "one" : "multi");

    if (!single) {
        vfio_pci_pre_reset(vdev);
    }
    vdev->vbasedev.needs_reset = false;

    info = g_malloc0(sizeof(*info));
    info->argsz = sizeof(*info);

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, info);
    if (ret && errno != ENOSPC) {
        ret = -errno;
        if (!vdev->has_pm_reset) {
            error_report("vfio: Cannot reset device %s, "
                         "no available reset mechanism.", vdev->vbasedev.name);
        }
        goto out_single;
    }

    count = info->count;
    info = g_realloc(info, sizeof(*info) + (count * sizeof(*devices)));
    info->argsz = sizeof(*info) + (count * sizeof(*devices));
    devices = &info->devices[0];

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, info);
    if (ret) {
        ret = -errno;
        error_report("vfio: hot reset info failed: %m");
        goto out_single;
    }

    trace_vfio_pci_hot_reset_has_dep_devices(vdev->vbasedev.name);

    /* Verify that we have all the groups required */
    for (i = 0; i < info->count; i++) {
        PCIHostDeviceAddress host;
        VFIOPCIDevice *tmp;
        VFIODevice *vbasedev_iter;

        host.domain = devices[i].segment;
        host.bus = devices[i].bus;
        host.slot = PCI_SLOT(devices[i].devfn);
        host.function = PCI_FUNC(devices[i].devfn);

        trace_vfio_pci_hot_reset_dep_devices(host.domain,
                host.bus, host.slot, host.function, devices[i].group_id);

        if (vfio_pci_host_match(&host, vdev->vbasedev.name)) {
            continue;
        }

        QLIST_FOREACH(group, &vfio_group_list, next) {
            if (group->groupid == devices[i].group_id) {
                break;
            }
        }

        if (!group) {
            if (!vdev->has_pm_reset) {
                error_report("vfio: Cannot reset device %s, "
                             "depends on group %d which is not owned.",
                             vdev->vbasedev.name, devices[i].group_id);
            }
            ret = -EPERM;
            goto out;
        }

        /* Prep dependent devices for reset and clear our marker. */
        QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
            if (!vbasedev_iter->dev->realized ||
                vbasedev_iter->type != VFIO_DEVICE_TYPE_PCI) {
                continue;
            }
            tmp = container_of(vbasedev_iter, VFIOPCIDevice, vbasedev);
            if (vfio_pci_host_match(&host, tmp->vbasedev.name)) {
                if (single) {
                    ret = -EINVAL;
                    goto out_single;
                }
                vfio_pci_pre_reset(tmp);
                tmp->vbasedev.needs_reset = false;
                multi = true;
                break;
            }
        }
    }

    if (!single && !multi) {
        ret = -EINVAL;
        goto out_single;
    }

    /* Determine how many group fds need to be passed */
    count = 0;
    QLIST_FOREACH(group, &vfio_group_list, next) {
        for (i = 0; i < info->count; i++) {
            if (group->groupid == devices[i].group_id) {
                count++;
                break;
            }
        }
    }

    reset = g_malloc0(sizeof(*reset) + (count * sizeof(*fds)));
    reset->argsz = sizeof(*reset) + (count * sizeof(*fds));
    fds = &reset->group_fds[0];

    /* Fill in group fds */
    QLIST_FOREACH(group, &vfio_group_list, next) {
        for (i = 0; i < info->count; i++) {
            if (group->groupid == devices[i].group_id) {
                fds[reset->count++] = group->fd;
                break;
            }
        }
    }

    /* Bus reset! */
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_PCI_HOT_RESET, reset);
    g_free(reset);

    trace_vfio_pci_hot_reset_result(vdev->vbasedev.name,
                                    ret ? "%m" : "Success");

out:
    /* Re-enable INTx on affected devices */
    for (i = 0; i < info->count; i++) {
        PCIHostDeviceAddress host;
        VFIOPCIDevice *tmp;
        VFIODevice *vbasedev_iter;

        host.domain = devices[i].segment;
        host.bus = devices[i].bus;
        host.slot = PCI_SLOT(devices[i].devfn);
        host.function = PCI_FUNC(devices[i].devfn);

        if (vfio_pci_host_match(&host, vdev->vbasedev.name)) {
            continue;
        }

        QLIST_FOREACH(group, &vfio_group_list, next) {
            if (group->groupid == devices[i].group_id) {
                break;
            }
        }

        if (!group) {
            break;
        }

        QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
            if (!vbasedev_iter->dev->realized ||
                vbasedev_iter->type != VFIO_DEVICE_TYPE_PCI) {
                continue;
            }
            tmp = container_of(vbasedev_iter, VFIOPCIDevice, vbasedev);
            if (vfio_pci_host_match(&host, tmp->vbasedev.name)) {
                vfio_pci_post_reset(tmp);
                break;
            }
        }
    }
out_single:
    if (!single) {
        vfio_pci_post_reset(vdev);
    }
    g_free(info);

    return ret;
}

/*
 * We want to differentiate hot reset of mulitple in-use devices vs hot reset
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

const VMStateDescription vmstate_vfio_pci_config = {
    .name = "VFIOPCIDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pdev, VFIOPCIDevice),
        VMSTATE_MSIX_TEST(pdev, VFIOPCIDevice, vfio_msix_present),
        VMSTATE_END_OF_LIST()
    }
};

static void vfio_pci_save_config(VFIODevice *vbasedev, QEMUFile *f)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    vmstate_save_state(f, &vmstate_vfio_pci_config, vdev, NULL);
}

static int vfio_pci_load_config(VFIODevice *vbasedev, QEMUFile *f)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    PCIDevice *pdev = &vdev->pdev;
    int ret;

    ret = vmstate_load_state(f, &vmstate_vfio_pci_config, vdev, 1);
    if (ret) {
        return ret;
    }

    vfio_pci_write_config(pdev, PCI_COMMAND,
                          pci_get_word(pdev->config + PCI_COMMAND), 2);

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
    .vfio_eoi = vfio_intx_eoi,
    .vfio_get_object = vfio_pci_get_object,
    .vfio_save_config = vfio_pci_save_config,
    .vfio_load_config = vfio_pci_load_config,
};

int vfio_populate_vga(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info;
    int ret;

    ret = vfio_get_region_info(vbasedev, VFIO_PCI_VGA_REGION_INDEX, &reg_info);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "failed getting region info for VGA region index %d",
                         VFIO_PCI_VGA_REGION_INDEX);
        return ret;
    }

    if (!(reg_info->flags & VFIO_REGION_INFO_FLAG_READ) ||
        !(reg_info->flags & VFIO_REGION_INFO_FLAG_WRITE) ||
        reg_info->size < 0xbffff + 1) {
        error_setg(errp, "unexpected VGA info, flags 0x%lx, size 0x%lx",
                   (unsigned long)reg_info->flags,
                   (unsigned long)reg_info->size);
        g_free(reg_info);
        return -EINVAL;
    }

    vdev->vga = g_new0(VFIOVGA, 1);

    vdev->vga->fd_offset = reg_info->offset;
    vdev->vga->fd = vdev->vbasedev.fd;

    g_free(reg_info);

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

    pci_register_vga(&vdev->pdev, &vdev->vga->region[QEMU_PCI_VGA_MEM].mem,
                     &vdev->vga->region[QEMU_PCI_VGA_IO_LO].mem,
                     &vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem);

    return 0;
}

static void vfio_populate_device(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info;
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info) };
    int i, ret = -1;

    /* Sanity check device */
    if (!(vbasedev->flags & VFIO_DEVICE_FLAGS_PCI)) {
        error_setg(errp, "this isn't a PCI device");
        return;
    }

    if (vbasedev->num_regions < VFIO_PCI_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "unexpected number of io regions %u",
                   vbasedev->num_regions);
        return;
    }

    if (vbasedev->num_irqs < VFIO_PCI_MSIX_IRQ_INDEX + 1) {
        error_setg(errp, "unexpected number of irqs %u", vbasedev->num_irqs);
        return;
    }

    for (i = VFIO_PCI_BAR0_REGION_INDEX; i < VFIO_PCI_ROM_REGION_INDEX; i++) {
        char *name = g_strdup_printf("%s BAR %d", vbasedev->name, i);

        ret = vfio_region_setup(OBJECT(vdev), vbasedev,
                                &vdev->bars[i].region, i, name);
        g_free(name);

        if (ret) {
            error_setg_errno(errp, -ret, "failed to get region %d info", i);
            return;
        }

        QLIST_INIT(&vdev->bars[i].quirks);
    }

    ret = vfio_get_region_info(vbasedev,
                               VFIO_PCI_CONFIG_REGION_INDEX, &reg_info);
    if (ret) {
        error_setg_errno(errp, -ret, "failed to get config info");
        return;
    }

    trace_vfio_populate_device_config(vdev->vbasedev.name,
                                      (unsigned long)reg_info->size,
                                      (unsigned long)reg_info->offset,
                                      (unsigned long)reg_info->flags);

    vdev->config_size = reg_info->size;
    if (vdev->config_size == PCI_CONFIG_SPACE_SIZE) {
        vdev->pdev.cap_present &= ~QEMU_PCI_CAP_EXPRESS;
    }
    vdev->config_offset = reg_info->offset;

    g_free(reg_info);

    if (vdev->features & VFIO_FEATURE_ENABLE_VGA) {
        ret = vfio_populate_vga(vdev, errp);
        if (ret) {
            error_append_hint(errp, "device does not support "
                              "requested feature x-vga\n");
            return;
        }
    }

    irq_info.index = VFIO_PCI_ERR_IRQ_INDEX;

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info);
    if (ret) {
        /* This can fail for an old kernel or legacy PCI dev */
        trace_vfio_populate_device_get_irq_info_failure(strerror(errno));
    } else if (irq_info.count == 1) {
        vdev->pci_aer = true;
    } else {
        warn_report(VFIO_MSG_PREFIX
                    "Could not enable error recovery for the device",
                    vbasedev->name);
    }
}

static void vfio_put_device(VFIOPCIDevice *vdev)
{
    g_free(vdev->vbasedev.name);
    g_free(vdev->msix);

    vfio_put_base_device(&vdev->vbasedev);
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
static void vfio_register_err_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;
    int32_t fd;

    if (!vdev->pci_aer) {
        return;
    }

    if (event_notifier_init(&vdev->err_notifier, 0)) {
        error_report("vfio: Unable to init event notifier for error detection");
        vdev->pci_aer = false;
        return;
    }

    fd = event_notifier_get_fd(&vdev->err_notifier);
    qemu_set_fd_handler(fd, vfio_err_notifier_handler, NULL, vdev);

    if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_ERR_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        event_notifier_cleanup(&vdev->err_notifier);
        vdev->pci_aer = false;
    }
}

static void vfio_unregister_err_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    if (!vdev->pci_aer) {
        return;
    }

    if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_ERR_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->err_notifier),
                        NULL, NULL, vdev);
    event_notifier_cleanup(&vdev->err_notifier);
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

static void vfio_register_req_notifier(VFIOPCIDevice *vdev)
{
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info),
                                      .index = VFIO_PCI_REQ_IRQ_INDEX };
    Error *err = NULL;
    int32_t fd;

    if (!(vdev->features & VFIO_FEATURE_ENABLE_REQ)) {
        return;
    }

    if (ioctl(vdev->vbasedev.fd,
              VFIO_DEVICE_GET_IRQ_INFO, &irq_info) < 0 || irq_info.count < 1) {
        return;
    }

    if (event_notifier_init(&vdev->req_notifier, 0)) {
        error_report("vfio: Unable to init event notifier for device request");
        return;
    }

    fd = event_notifier_get_fd(&vdev->req_notifier);
    qemu_set_fd_handler(fd, vfio_req_notifier_handler, NULL, vdev);

    if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX, 0,
                           VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        event_notifier_cleanup(&vdev->req_notifier);
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

    if (vfio_set_irq_signaling(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX, 0,
                               VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->req_notifier),
                        NULL, NULL, vdev);
    event_notifier_cleanup(&vdev->req_notifier);

    vdev->req_enabled = false;
}

static void vfio_realize(PCIDevice *pdev, Error **errp)
{
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);
    VFIODevice *vbasedev_iter;
    VFIOGroup *group;
    char *tmp, *subsys, group_path[PATH_MAX], *group_name;
    Error *err = NULL;
    ssize_t len;
    struct stat st;
    int groupid;
    int i, ret;
    bool is_mdev;

    if (!vdev->vbasedev.sysfsdev) {
        if (!(~vdev->host.domain || ~vdev->host.bus ||
              ~vdev->host.slot || ~vdev->host.function)) {
            error_setg(errp, "No provided host device");
            error_append_hint(errp, "Use -device vfio-pci,host=DDDD:BB:DD.F "
                              "or -device vfio-pci,sysfsdev=PATH_TO_DEVICE\n");
            return;
        }
        vdev->vbasedev.sysfsdev =
            g_strdup_printf("/sys/bus/pci/devices/%04x:%02x:%02x.%01x",
                            vdev->host.domain, vdev->host.bus,
                            vdev->host.slot, vdev->host.function);
    }

    if (stat(vdev->vbasedev.sysfsdev, &st) < 0) {
        error_setg_errno(errp, errno, "no such host device");
        error_prepend(errp, VFIO_MSG_PREFIX, vdev->vbasedev.sysfsdev);
        return;
    }

    vdev->vbasedev.name = g_path_get_basename(vdev->vbasedev.sysfsdev);
    vdev->vbasedev.ops = &vfio_pci_ops;
    vdev->vbasedev.type = VFIO_DEVICE_TYPE_PCI;
    vdev->vbasedev.dev = DEVICE(vdev);

    tmp = g_strdup_printf("%s/iommu_group", vdev->vbasedev.sysfsdev);
    len = readlink(tmp, group_path, sizeof(group_path));
    g_free(tmp);

    if (len <= 0 || len >= sizeof(group_path)) {
        error_setg_errno(errp, len < 0 ? errno : ENAMETOOLONG,
                         "no iommu_group found");
        goto error;
    }

    group_path[len] = 0;

    group_name = basename(group_path);
    if (sscanf(group_name, "%d", &groupid) != 1) {
        error_setg_errno(errp, errno, "failed to read %s", group_path);
        goto error;
    }

    trace_vfio_realize(vdev->vbasedev.name, groupid);

    group = vfio_get_group(groupid, pci_device_iommu_address_space(pdev), errp);
    if (!group) {
        goto error;
    }

    QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
        if (strcmp(vbasedev_iter->name, vdev->vbasedev.name) == 0) {
            error_setg(errp, "device is already attached");
            vfio_put_group(group);
            goto error;
        }
    }

    /*
     * Mediated devices *might* operate compatibly with discarding of RAM, but
     * we cannot know for certain, it depends on whether the mdev vendor driver
     * stays in sync with the active working set of the guest driver.  Prevent
     * the x-balloon-allowed option unless this is minimally an mdev device.
     */
    tmp = g_strdup_printf("%s/subsystem", vdev->vbasedev.sysfsdev);
    subsys = realpath(tmp, NULL);
    g_free(tmp);
    is_mdev = subsys && (strcmp(subsys, "/sys/bus/mdev") == 0);
    free(subsys);

    trace_vfio_mdev(vdev->vbasedev.name, is_mdev);

    if (vdev->vbasedev.ram_block_discard_allowed && !is_mdev) {
        error_setg(errp, "x-balloon-allowed only potentially compatible "
                   "with mdev devices");
        vfio_put_group(group);
        goto error;
    }

    ret = vfio_get_device(group, vdev->vbasedev.name, &vdev->vbasedev, errp);
    if (ret) {
        vfio_put_group(group);
        goto error;
    }

    vfio_populate_device(vdev, &err);
    if (err) {
        error_propagate(errp, err);
        goto error;
    }

    /* Get a copy of config space */
    ret = pread(vdev->vbasedev.fd, vdev->pdev.config,
                MIN(pci_config_size(&vdev->pdev), vdev->config_size),
                vdev->config_offset);
    if (ret < (int)MIN(pci_config_size(&vdev->pdev), vdev->config_size)) {
        ret = ret < 0 ? -errno : -EFAULT;
        error_setg_errno(errp, -ret, "failed to read device config space");
        goto error;
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
            goto error;
        }
        vfio_add_emulated_word(vdev, PCI_VENDOR_ID, vdev->vendor_id, ~0);
        trace_vfio_pci_emulated_vendor_id(vdev->vbasedev.name, vdev->vendor_id);
    } else {
        vdev->vendor_id = pci_get_word(pdev->config + PCI_VENDOR_ID);
    }

    if (vdev->device_id != PCI_ANY_ID) {
        if (vdev->device_id > 0xffff) {
            error_setg(errp, "invalid PCI device ID provided");
            goto error;
        }
        vfio_add_emulated_word(vdev, PCI_DEVICE_ID, vdev->device_id, ~0);
        trace_vfio_pci_emulated_device_id(vdev->vbasedev.name, vdev->device_id);
    } else {
        vdev->device_id = pci_get_word(pdev->config + PCI_DEVICE_ID);
    }

    if (vdev->sub_vendor_id != PCI_ANY_ID) {
        if (vdev->sub_vendor_id > 0xffff) {
            error_setg(errp, "invalid PCI subsystem vendor ID provided");
            goto error;
        }
        vfio_add_emulated_word(vdev, PCI_SUBSYSTEM_VENDOR_ID,
                               vdev->sub_vendor_id, ~0);
        trace_vfio_pci_emulated_sub_vendor_id(vdev->vbasedev.name,
                                              vdev->sub_vendor_id);
    }

    if (vdev->sub_device_id != PCI_ANY_ID) {
        if (vdev->sub_device_id > 0xffff) {
            error_setg(errp, "invalid PCI subsystem device ID provided");
            goto error;
        }
        vfio_add_emulated_word(vdev, PCI_SUBSYSTEM_ID, vdev->sub_device_id, ~0);
        trace_vfio_pci_emulated_sub_device_id(vdev->vbasedev.name,
                                              vdev->sub_device_id);
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

    vfio_msix_early_setup(vdev, &err);
    if (err) {
        error_propagate(errp, err);
        goto error;
    }

    vfio_bars_register(vdev);

    ret = vfio_add_capabilities(vdev, errp);
    if (ret) {
        goto out_teardown;
    }

    if (vdev->vga) {
        vfio_vga_quirk_setup(vdev);
    }

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_quirk_setup(vdev, i);
    }

    if (!vdev->igd_opregion &&
        vdev->features & VFIO_FEATURE_ENABLE_IGD_OPREGION) {
        struct vfio_region_info *opregion;

        if (vdev->pdev.qdev.hotplugged) {
            error_setg(errp,
                       "cannot support IGD OpRegion feature on hotplugged "
                       "device");
            goto out_teardown;
        }

        ret = vfio_get_dev_region_info(&vdev->vbasedev,
                        VFIO_REGION_TYPE_PCI_VENDOR_TYPE | PCI_VENDOR_ID_INTEL,
                        VFIO_REGION_SUBTYPE_INTEL_IGD_OPREGION, &opregion);
        if (ret) {
            error_setg_errno(errp, -ret,
                             "does not support requested IGD OpRegion feature");
            goto out_teardown;
        }

        ret = vfio_pci_igd_opregion_init(vdev, opregion, errp);
        g_free(opregion);
        if (ret) {
            goto out_teardown;
        }
    }

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
        ret = vfio_intx_enable(vdev, errp);
        if (ret) {
            goto out_deregister;
        }
    }

    if (vdev->display != ON_OFF_AUTO_OFF) {
        ret = vfio_display_probe(vdev, errp);
        if (ret) {
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

    if (vdev->vendor_id == PCI_VENDOR_ID_NVIDIA) {
        ret = vfio_pci_nvidia_v100_ram_init(vdev, errp);
        if (ret && ret != -ENODEV) {
            error_report("Failed to setup NVIDIA V100 GPU RAM");
        }
    }

    if (vdev->vendor_id == PCI_VENDOR_ID_IBM) {
        ret = vfio_pci_nvlink2_init(vdev, errp);
        if (ret && ret != -ENODEV) {
            error_report("Failed to setup NVlink2 bridge");
        }
    }

    if (!pdev->failover_pair_id) {
        ret = vfio_migration_probe(&vdev->vbasedev, errp);
        if (ret) {
            error_report("%s: Migration disabled", vdev->vbasedev.name);
        }
    }

    vfio_register_err_notifier(vdev);
    vfio_register_req_notifier(vdev);
    vfio_setup_resetfn_quirk(vdev);

    return;

out_deregister:
    pci_device_set_intx_routing_notifier(&vdev->pdev, NULL);
    kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
out_teardown:
    vfio_teardown_msi(vdev);
    vfio_bars_exit(vdev);
error:
    error_prepend(errp, VFIO_MSG_PREFIX, vdev->vbasedev.name);
}

static void vfio_instance_finalize(Object *obj)
{
    VFIOPCIDevice *vdev = VFIO_PCI(obj);
    VFIOGroup *group = vdev->vbasedev.group;

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
    vfio_put_device(vdev);
    vfio_put_group(group);
}

static void vfio_exitfn(PCIDevice *pdev)
{
    VFIOPCIDevice *vdev = VFIO_PCI(pdev);

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
    vfio_teardown_msi(vdev);
    vfio_bars_exit(vdev);
    vfio_migration_finalize(&vdev->vbasedev);
}

static void vfio_pci_reset(DeviceState *dev)
{
    VFIOPCIDevice *vdev = VFIO_PCI(dev);

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
    VFIOPCIDevice *vdev = VFIO_PCI(obj);

    device_add_bootindex_property(obj, &vdev->bootindex,
                                  "bootindex", NULL,
                                  &pci_dev->qdev);
    vdev->host.domain = ~0U;
    vdev->host.bus = ~0U;
    vdev->host.slot = ~0U;
    vdev->host.function = ~0U;

    vdev->nv_gpudirect_clique = 0xFF;

    /* QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices */
    pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static Property vfio_pci_dev_properties[] = {
    DEFINE_PROP_PCI_HOST_DEVADDR("host", VFIOPCIDevice, host),
    DEFINE_PROP_STRING("sysfsdev", VFIOPCIDevice, vbasedev.sysfsdev),
    DEFINE_PROP_ON_OFF_AUTO("x-pre-copy-dirty-page-tracking", VFIOPCIDevice,
                            vbasedev.pre_copy_dirty_page_tracking,
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
                    VFIO_FEATURE_ENABLE_IGD_OPREGION_BIT, false),
    DEFINE_PROP_BOOL("x-enable-migration", VFIOPCIDevice,
                     vbasedev.enable_migration, false),
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
    DEFINE_PROP_UINT32("x-igd-gms", VFIOPCIDevice, igd_gms, 0),
    DEFINE_PROP_UNSIGNED_NODEFAULT("x-nv-gpudirect-clique", VFIOPCIDevice,
                                   nv_gpudirect_clique,
                                   qdev_prop_nv_gpudirect_clique, uint8_t),
    DEFINE_PROP_OFF_AUTO_PCIBAR("x-msix-relocation", VFIOPCIDevice, msix_relo,
                                OFF_AUTOPCIBAR_OFF),
    /*
     * TODO - support passed fds... is this necessary?
     * DEFINE_PROP_STRING("vfiofd", VFIOPCIDevice, vfiofd_name),
     * DEFINE_PROP_STRING("vfiogroupfd, VFIOPCIDevice, vfiogroupfd_name),
     */
    DEFINE_PROP_END_OF_LIST(),
};

static void vfio_pci_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    dc->reset = vfio_pci_reset;
    device_class_set_props(dc, vfio_pci_dev_properties);
    dc->desc = "VFIO-based PCI device assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    pdc->realize = vfio_realize;
    pdc->exit = vfio_exitfn;
    pdc->config_read = vfio_pci_read_config;
    pdc->config_write = vfio_pci_write_config;
}

static const TypeInfo vfio_pci_dev_info = {
    .name = TYPE_VFIO_PCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VFIOPCIDevice),
    .class_init = vfio_pci_dev_class_init,
    .instance_init = vfio_instance_init,
    .instance_finalize = vfio_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static Property vfio_pci_dev_nohotplug_properties[] = {
    DEFINE_PROP_BOOL("ramfb", VFIOPCIDevice, enable_ramfb, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vfio_pci_nohotplug_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_pci_dev_nohotplug_properties);
    dc->hotpluggable = false;
}

static const TypeInfo vfio_pci_nohotplug_dev_info = {
    .name = TYPE_VFIO_PCI_NOHOTPLUG,
    .parent = TYPE_VFIO_PCI,
    .instance_size = sizeof(VFIOPCIDevice),
    .class_init = vfio_pci_nohotplug_dev_class_init,
};

static void register_vfio_pci_dev_type(void)
{
    type_register_static(&vfio_pci_dev_info);
    type_register_static(&vfio_pci_nohotplug_dev_info);
}

type_init(register_vfio_pci_dev_type)
