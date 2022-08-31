/*
 * libqos Generic PCI bindings and generic pci host bridge
 *
 * Copyright Red Hat Inc., 2022
 *
 * Authors:
 *  Eric Auger <eric.auger@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_GENERIC_PCIHOST_H
#define LIBQOS_GENERIC_PCIHOST_H

#include "pci.h"
#include "libqos-malloc.h"
#include "qgraph.h"

typedef struct QGenericPCIBus {
    QOSGraphObject obj;
    QPCIBus bus;
    uint64_t gpex_pio_base;
    uint64_t ecam_alloc_ptr;
} QGenericPCIBus;

/*
 * qpci_init_generic():
 * @ret: A valid QGenericPCIBus * pointer
 * @qts: The %QTestState
 * @alloc: A previously initialized @alloc providing memory for @qts
 * @bool: devices can be hotplugged on this bus
 *
 * This function initializes an already allocated
 * QGenericPCIBus object.
 */
void qpci_init_generic(QGenericPCIBus *ret, QTestState *qts,
                       QGuestAllocator *alloc, bool hotpluggable);

/* QGenericPCIHost */

typedef struct QGenericPCIHost QGenericPCIHost;

struct QGenericPCIHost {
    QOSGraphObject obj;
    QGenericPCIBus pci;
};

QOSGraphObject *generic_pcihost_get_device(void *obj, const char *device);
void qos_create_generic_pcihost(QGenericPCIHost *host,
                                QTestState *qts,
                                QGuestAllocator *alloc);

#endif
