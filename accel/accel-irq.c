/*
 * Accelerated irqchip abstraction
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/pci/msi.h"

#include "system/kvm.h"
#include "system/mshv.h"
#include "system/accel-irq.h"

int accel_irqchip_add_msi_route(AccelRouteChange *c, int vector, PCIDevice *dev)
{
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_add_msi_route(c, vector, dev);
    }
    if (kvm_enabled()) {
        return kvm_irqchip_add_msi_route(c, vector, dev);
    }
    return -ENOSYS;
}

int accel_irqchip_update_msi_route(int vector, MSIMessage msg, PCIDevice *dev)
{
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_update_msi_route(vector, msg, dev);
    }
    if (kvm_enabled()) {
        return kvm_irqchip_update_msi_route(kvm_state, vector, msg, dev);
    }
    return -ENOSYS;
}

void accel_irqchip_commit_route_changes(AccelRouteChange *c)
{
    if (c->changes) {
        accel_irqchip_commit_routes();
        c->changes = 0;
    }
}

void accel_irqchip_commit_routes(void)
{
    if (mshv_msi_via_irqfd_enabled()) {
        mshv_irqchip_commit_routes(mshv_state);
    }
    if (kvm_enabled()) {
        kvm_irqchip_commit_routes(kvm_state);
    }
}

void accel_irqchip_release_virq(int virq)
{
    if (mshv_msi_via_irqfd_enabled()) {
        mshv_irqchip_release_virq(mshv_state, virq);
    }
    if (kvm_enabled()) {
        kvm_irqchip_release_virq(kvm_state, virq);
    }
}

int accel_irqchip_add_irqfd_notifier_gsi(EventNotifier *n, EventNotifier *rn,
                                         int virq)
{
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_add_irqfd_notifier_gsi(n, rn, virq);
    }
    if (kvm_enabled()) {
        return kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, rn, virq);
    }
    return -ENOSYS;
}

int accel_irqchip_remove_irqfd_notifier_gsi(EventNotifier *n, int virq)
{
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_remove_irqfd_notifier_gsi(n, virq);
    }
    if (kvm_enabled()) {
        return kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, n, virq);
    }
    return -ENOSYS;
}

inline AccelRouteChange accel_irqchip_begin_route_changes(void)
{
    if (mshv_msi_via_irqfd_enabled()) {
        return (AccelRouteChange) {
            .accel = ACCEL(mshv_state),
            .changes = 0,
        };
    }
    if (kvm_enabled()) {
        return (AccelRouteChange) {
            .accel = ACCEL(kvm_state),
            .changes = 0,
        };
    }
    error_report("can't initiate route change, no accel irqchip available");
    abort();
}
