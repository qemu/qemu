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

#ifndef TESTS_LIBQOS_VIRTIO_SCSI_H
#define TESTS_LIBQOS_VIRTIO_SCSI_H

#include "libqos/qgraph.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"

typedef struct QVirtioSCSI QVirtioSCSI;
typedef struct QVirtioSCSIPCI QVirtioSCSIPCI;
typedef struct QVirtioSCSIDevice QVirtioSCSIDevice;

struct QVirtioSCSI {
    QVirtioDevice *vdev;
};

struct QVirtioSCSIPCI {
    QVirtioPCIDevice pci_vdev;
    QVirtioSCSI scsi;
};

struct QVirtioSCSIDevice {
    QOSGraphObject obj;
    QVirtioSCSI scsi;
};

#endif
