/*
 * Remote IO Hub
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_bus.h"
#include "qemu/thread.h"
#include "hw/remote/machine.h"
#include "hw/remote/iohub.h"
#include "qemu/main-loop.h"

void remote_iohub_init(RemoteIOHubState *iohub)
{
    int pirq;

    memset(&iohub->irqfds, 0, sizeof(iohub->irqfds));
    memset(&iohub->resamplefds, 0, sizeof(iohub->resamplefds));

    for (pirq = 0; pirq < REMOTE_IOHUB_NB_PIRQS; pirq++) {
        qemu_mutex_init(&iohub->irq_level_lock[pirq]);
        iohub->irq_level[pirq] = 0;
        event_notifier_init_fd(&iohub->irqfds[pirq], -1);
        event_notifier_init_fd(&iohub->resamplefds[pirq], -1);
    }
}

void remote_iohub_finalize(RemoteIOHubState *iohub)
{
    int pirq;

    for (pirq = 0; pirq < REMOTE_IOHUB_NB_PIRQS; pirq++) {
        qemu_set_fd_handler(event_notifier_get_fd(&iohub->resamplefds[pirq]),
                            NULL, NULL, NULL);
        event_notifier_cleanup(&iohub->irqfds[pirq]);
        event_notifier_cleanup(&iohub->resamplefds[pirq]);
        qemu_mutex_destroy(&iohub->irq_level_lock[pirq]);
    }
}

int remote_iohub_map_irq(PCIDevice *pci_dev, int intx)
{
    return pci_dev->devfn;
}

void remote_iohub_set_irq(void *opaque, int pirq, int level)
{
    RemoteIOHubState *iohub = opaque;

    assert(pirq >= 0);
    assert(pirq < PCI_DEVFN_MAX);

    QEMU_LOCK_GUARD(&iohub->irq_level_lock[pirq]);

    if (level) {
        if (++iohub->irq_level[pirq] == 1) {
            event_notifier_set(&iohub->irqfds[pirq]);
        }
    } else if (iohub->irq_level[pirq] > 0) {
        iohub->irq_level[pirq]--;
    }
}

static void intr_resample_handler(void *opaque)
{
    ResampleToken *token = opaque;
    RemoteIOHubState *iohub = token->iohub;
    int pirq, s;

    pirq = token->pirq;

    s = event_notifier_test_and_clear(&iohub->resamplefds[pirq]);

    assert(s >= 0);

    QEMU_LOCK_GUARD(&iohub->irq_level_lock[pirq]);

    if (iohub->irq_level[pirq]) {
        event_notifier_set(&iohub->irqfds[pirq]);
    }
}

void process_set_irqfd_msg(PCIDevice *pci_dev, MPQemuMsg *msg)
{
    RemoteMachineState *machine = REMOTE_MACHINE(current_machine);
    RemoteIOHubState *iohub = &machine->iohub;
    int pirq, intx;

    intx = pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;

    pirq = remote_iohub_map_irq(pci_dev, intx);

    if (event_notifier_get_fd(&iohub->irqfds[pirq]) != -1) {
        qemu_set_fd_handler(event_notifier_get_fd(&iohub->resamplefds[pirq]),
                            NULL, NULL, NULL);
        event_notifier_cleanup(&iohub->irqfds[pirq]);
        event_notifier_cleanup(&iohub->resamplefds[pirq]);
        memset(&iohub->token[pirq], 0, sizeof(ResampleToken));
    }

    event_notifier_init_fd(&iohub->irqfds[pirq], msg->fds[0]);
    event_notifier_init_fd(&iohub->resamplefds[pirq], msg->fds[1]);

    iohub->token[pirq].iohub = iohub;
    iohub->token[pirq].pirq = pirq;

    qemu_set_fd_handler(msg->fds[1], intr_resample_handler, NULL,
                        &iohub->token[pirq]);
}
