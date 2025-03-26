/*
 * VFIO CPR
 *
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_CPR_H
#define HW_VFIO_CPR_H

bool vfio_cpr_register_container(VFIOContainerBase *bcontainer, Error **errp);
void vfio_cpr_unregister_container(VFIOContainerBase *bcontainer);

#endif /* HW_VFIO_CPR_H */
