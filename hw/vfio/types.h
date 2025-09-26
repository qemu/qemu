/*
 * VFIO types definition
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_VFIO_VFIO_TYPES_H
#define HW_VFIO_VFIO_TYPES_H

/*
 * TYPE_VFIO_PCI_DEVICE is an abstract type used to share code
 * between VFIO implementations that use a kernel driver
 * with those that use user sockets.
 */
#define TYPE_VFIO_PCI_DEVICE "vfio-pci-device"

#define TYPE_VFIO_PCI "vfio-pci"
/* TYPE_VFIO_PCI shares struct VFIOPCIDevice. */

#define TYPE_VFIO_PCI_NOHOTPLUG "vfio-pci-nohotplug"

#endif /* HW_VFIO_VFIO_TYPES_H */
