/*
 * libqos driver framework
 *
 * Based on tests/qtest/libqos/virtio-blk.c
 *
 * Copyright (c) 2020 Coiby Xu <coiby.xu@gmail.com>
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef TESTS_LIBQOS_VHOST_USER_BLK_H
#define TESTS_LIBQOS_VHOST_USER_BLK_H

#include "qgraph.h"
#include "virtio.h"
#include "virtio-pci.h"

typedef struct QVhostUserBlk QVhostUserBlk;
typedef struct QVhostUserBlkPCI QVhostUserBlkPCI;
typedef struct QVhostUserBlkDevice QVhostUserBlkDevice;

struct QVhostUserBlk {
    QVirtioDevice *vdev;
};

struct QVhostUserBlkPCI {
    QVirtioPCIDevice pci_vdev;
    QVhostUserBlk blk;
};

struct QVhostUserBlkDevice {
    QOSGraphObject obj;
    QVhostUserBlk blk;
};

#endif
