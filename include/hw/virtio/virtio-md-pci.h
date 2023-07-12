/*
 * Abstract virtio based memory device
 *
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_MD_PCI_H
#define HW_VIRTIO_MD_PCI_H

#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"

/*
 * virtio-md-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_MD_PCI "virtio-md-pci"

OBJECT_DECLARE_TYPE(VirtIOMDPCI, VirtIOMDPCIClass, VIRTIO_MD_PCI)

struct VirtIOMDPCIClass {
    /* private */
    VirtioPCIClass parent;

    /* public */
    void (*unplug_request_check)(VirtIOMDPCI *vmd, Error **errp);
};

struct VirtIOMDPCI {
    VirtIOPCIProxy parent_obj;
};

void virtio_md_pci_pre_plug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp);
void virtio_md_pci_plug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp);
void virtio_md_pci_unplug_request(VirtIOMDPCI *vmd, MachineState *ms,
                                  Error **errp);
void virtio_md_pci_unplug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp);

#endif
