/*
 * QEMU MSHV stub
 *
 * Copyright Red Hat, Inc. 2025
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/msi.h"
#include "system/mshv.h"

bool mshv_allowed;

int mshv_irqchip_add_msi_route(int vector, PCIDevice *dev)
{
    return -ENOSYS;
}

void mshv_irqchip_release_virq(int virq)
{
}

int mshv_irqchip_update_msi_route(int virq, MSIMessage msg, PCIDevice *dev)
{
    return -ENOSYS;
}

void mshv_irqchip_commit_routes(void)
{
}

int mshv_irqchip_add_irqfd_notifier_gsi(const EventNotifier *n,
                                        const EventNotifier *rn, int virq)
{
    return -ENOSYS;
}

int mshv_irqchip_remove_irqfd_notifier_gsi(const EventNotifier *n, int virq)
{
    return -ENOSYS;
}
