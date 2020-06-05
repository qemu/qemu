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

#ifndef TESTS_LIBQOS_VIRTIO_BLK_H
#define TESTS_LIBQOS_VIRTIO_BLK_H

#include "libqos/qgraph.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"

typedef struct QVirtioBlk QVirtioBlk;
typedef struct QVirtioBlkPCI QVirtioBlkPCI;
typedef struct QVirtioBlkDevice QVirtioBlkDevice;

/* virtqueue is created in each test */
struct QVirtioBlk {
    QVirtioDevice *vdev;
};

struct QVirtioBlkPCI {
    QVirtioPCIDevice pci_vdev;
    QVirtioBlk blk;
};

struct QVirtioBlkDevice {
    QOSGraphObject obj;
    QVirtioBlk blk;
};

#endif
