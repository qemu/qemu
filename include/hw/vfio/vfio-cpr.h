/*
 * VFIO CPR
 *
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CPR_H
#define HW_VFIO_VFIO_CPR_H

#include "migration/misc.h"

struct VFIOContainer;
struct VFIOContainerBase;

typedef struct VFIOContainerCPR {
    Error *blocker;
} VFIOContainerCPR;


bool vfio_legacy_cpr_register_container(struct VFIOContainer *container,
                                        Error **errp);
void vfio_legacy_cpr_unregister_container(struct VFIOContainer *container);

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp);

bool vfio_cpr_register_container(struct VFIOContainerBase *bcontainer,
                                 Error **errp);
void vfio_cpr_unregister_container(struct VFIOContainerBase *bcontainer);

#endif /* HW_VFIO_VFIO_CPR_H */
