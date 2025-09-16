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
#include "hw/pci/msi.h"

#include "system/kvm.h"
#include "system/mshv.h"
#include "system/accel-irq.h"

int accel_irqchip_add_msi_route(KVMRouteChange *c, int vector, PCIDevice *dev)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_add_msi_route(vector, dev);
    }
#endif
    if (kvm_enabled()) {
        return kvm_irqchip_add_msi_route(c, vector, dev);
    }
    return -ENOSYS;
}

int accel_irqchip_update_msi_route(int vector, MSIMessage msg, PCIDevice *dev)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_update_msi_route(vector, msg, dev);
    }
#endif
    if (kvm_enabled()) {
        return kvm_irqchip_update_msi_route(kvm_state, vector, msg, dev);
    }
    return -ENOSYS;
}

void accel_irqchip_commit_route_changes(KVMRouteChange *c)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        mshv_irqchip_commit_routes();
    }
#endif
    if (kvm_enabled()) {
        kvm_irqchip_commit_route_changes(c);
    }
}

void accel_irqchip_commit_routes(void)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        mshv_irqchip_commit_routes();
    }
#endif
    if (kvm_enabled()) {
        kvm_irqchip_commit_routes(kvm_state);
    }
}

void accel_irqchip_release_virq(int virq)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        mshv_irqchip_release_virq(virq);
    }
#endif
    if (kvm_enabled()) {
        kvm_irqchip_release_virq(kvm_state, virq);
    }
}

int accel_irqchip_add_irqfd_notifier_gsi(EventNotifier *n, EventNotifier *rn,
                                         int virq)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_add_irqfd_notifier_gsi(n, rn, virq);
    }
#endif
    if (kvm_enabled()) {
        return kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, rn, virq);
    }
    return -ENOSYS;
}

int accel_irqchip_remove_irqfd_notifier_gsi(EventNotifier *n, int virq)
{
#ifdef CONFIG_MSHV_IS_POSSIBLE
    if (mshv_msi_via_irqfd_enabled()) {
        return mshv_irqchip_remove_irqfd_notifier_gsi(n, virq);
    }
#endif
    if (kvm_enabled()) {
        return kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, n, virq);
    }
    return -ENOSYS;
}
