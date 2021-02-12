/*
 * IO Hub for remote device
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_IOHUB_H
#define REMOTE_IOHUB_H

#include "hw/pci/pci.h"
#include "qemu/event_notifier.h"
#include "qemu/thread-posix.h"
#include "hw/remote/mpqemu-link.h"

#define REMOTE_IOHUB_NB_PIRQS    PCI_DEVFN_MAX

typedef struct ResampleToken {
    void *iohub;
    int pirq;
} ResampleToken;

typedef struct RemoteIOHubState {
    PCIDevice d;
    EventNotifier irqfds[REMOTE_IOHUB_NB_PIRQS];
    EventNotifier resamplefds[REMOTE_IOHUB_NB_PIRQS];
    unsigned int irq_level[REMOTE_IOHUB_NB_PIRQS];
    ResampleToken token[REMOTE_IOHUB_NB_PIRQS];
    QemuMutex irq_level_lock[REMOTE_IOHUB_NB_PIRQS];
} RemoteIOHubState;

int remote_iohub_map_irq(PCIDevice *pci_dev, int intx);
void remote_iohub_set_irq(void *opaque, int pirq, int level);
void process_set_irqfd_msg(PCIDevice *pci_dev, MPQemuMsg *msg);

void remote_iohub_init(RemoteIOHubState *iohub);
void remote_iohub_finalize(RemoteIOHubState *iohub);

#endif
