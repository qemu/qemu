/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou  <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Jinank Jain  <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_H
#define QEMU_MSHV_H

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "hw/hyperv/hyperv-proto.h"
#include "hw/hyperv/hvhdk.h"
#include "qapi/qapi-types-common.h"
#include "system/memory.h"
#include "accel/accel-ops.h"

#ifdef COMPILING_PER_TARGET
#ifdef CONFIG_MSHV
#include <linux/mshv.h>
#define CONFIG_MSHV_IS_POSSIBLE
#endif
#else
#define CONFIG_MSHV_IS_POSSIBLE
#endif

#ifdef CONFIG_MSHV_IS_POSSIBLE
extern bool mshv_allowed;
#define mshv_enabled() (mshv_allowed)
#else /* CONFIG_MSHV_IS_POSSIBLE */
#define mshv_enabled() false
#endif
#define mshv_msi_via_irqfd_enabled() false

typedef struct MshvState MshvState;
extern MshvState *mshv_state;

/* interrupt */
int mshv_irqchip_add_msi_route(int vector, PCIDevice *dev);
int mshv_irqchip_update_msi_route(int virq, MSIMessage msg, PCIDevice *dev);
void mshv_irqchip_commit_routes(void);
void mshv_irqchip_release_virq(int virq);
int mshv_irqchip_add_irqfd_notifier_gsi(const EventNotifier *n,
                                        const EventNotifier *rn, int virq);
int mshv_irqchip_remove_irqfd_notifier_gsi(const EventNotifier *n, int virq);

#endif
