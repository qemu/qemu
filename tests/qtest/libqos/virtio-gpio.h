/*
 * virtio-gpio structures
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TESTS_LIBQOS_VIRTIO_GPIO_H
#define TESTS_LIBQOS_VIRTIO_GPIO_H

#include "qgraph.h"
#include "virtio.h"
#include "virtio-pci.h"

typedef struct QVhostUserGPIO QVhostUserGPIO;
typedef struct QVhostUserGPIOPCI QVhostUserGPIOPCI;
typedef struct QVhostUserGPIODevice QVhostUserGPIODevice;

struct QVhostUserGPIO {
    QVirtioDevice *vdev;
    QVirtQueue **queues;
};

struct QVhostUserGPIOPCI {
    QVirtioPCIDevice pci_vdev;
    QVhostUserGPIO gpio;
};

struct QVhostUserGPIODevice {
    QOSGraphObject obj;
    QVhostUserGPIO gpio;
};

#endif
