/*
 * virtio-scmi structures
 *
 * SPDX-FileCopyrightText: Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TESTS_LIBQOS_VIRTIO_SCMI_H
#define TESTS_LIBQOS_VIRTIO_SCMI_H

#include "qgraph.h"
#include "virtio.h"
#include "virtio-pci.h"

typedef struct QVhostUserSCMI QVhostUserSCMI;
typedef struct QVhostUserSCMIPCI QVhostUserSCMIPCI;
typedef struct QVhostUserSCMIDevice QVhostUserSCMIDevice;

struct QVhostUserSCMI {
    QVirtioDevice *vdev;
    QVirtQueue **queues;
};

struct QVhostUserSCMIPCI {
    QVirtioPCIDevice pci_vdev;
    QVhostUserSCMI scmi;
};

struct QVhostUserSCMIDevice {
    QOSGraphObject obj;
    QVhostUserSCMI scmi;
};

#endif
