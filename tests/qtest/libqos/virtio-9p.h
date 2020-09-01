/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef TESTS_LIBQOS_VIRTIO_9P_H
#define TESTS_LIBQOS_VIRTIO_9P_H

#include "qgraph.h"
#include "virtio.h"
#include "virtio-pci.h"

typedef struct QVirtio9P QVirtio9P;
typedef struct QVirtio9PPCI QVirtio9PPCI;
typedef struct QVirtio9PDevice QVirtio9PDevice;

#define MOUNT_TAG "qtest"

struct QVirtio9P {
    QVirtioDevice *vdev;
    QVirtQueue *vq;
};

struct QVirtio9PPCI {
    QVirtioPCIDevice pci_vdev;
    QVirtio9P v9p;
};

struct QVirtio9PDevice {
    QOSGraphObject obj;
    QVirtio9P v9p;
};

#endif
