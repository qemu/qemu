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

#include "qemu/osdep.h"
#include "hw/virtio/virtio-md-pci.h"
#include "hw/mem/memory-device.h"

static const TypeInfo virtio_md_pci_info = {
    .name = TYPE_VIRTIO_MD_PCI,
    .parent = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOMDPCI),
    .class_size = sizeof(VirtIOMDPCIClass),
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void virtio_md_pci_register(void)
{
    type_register_static(&virtio_md_pci_info);
}
type_init(virtio_md_pci_register)
