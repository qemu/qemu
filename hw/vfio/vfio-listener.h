/*
 * VFIO MemoryListener services
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_LISTENER_H
#define HW_VFIO_VFIO_LISTENER_H

bool vfio_listener_register(VFIOContainer *bcontainer, Error **errp);
void vfio_listener_unregister(VFIOContainer *bcontainer);

#endif /* HW_VFIO_VFIO_LISTENER_H */
