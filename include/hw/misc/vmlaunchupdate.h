/*
 * Guest driven VM launch state update device via IGVM.
 * For details and specification, please look at docs/specs/vmlaunchupdate.rst.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef VMLAUNCHUPDATE_H
#define VMLAUNCHUPDATE_H

#include "hw/core/qdev.h"
#include "qom/object.h"
#include "qemu/units.h"
#include "system/igvm-cfg.h"
#include "standard-headers/misc/vmlaunchupdate.h"

#define TYPE_VMLAUNCHUPDATE "vm-launch-update"

typedef struct VMLaunchUpdateState {
    DeviceState parent_obj;
    VMLaunchUpdate launch_update;
    bool disabled;
    bool host_igvm_on_reset;
    ResettableState reset_state;
} VMLaunchUpdateState;


typedef struct VMLaunchUpdateStateClass {
    ObjectClass parent_class;
} VMLaunchUpdateStateClass;

OBJECT_DECLARE_SIMPLE_TYPE(VMLaunchUpdateState, VMLAUNCHUPDATE);

#endif
