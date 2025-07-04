/*
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "migration/cpr.h"
#include "migration/vmstate.h"

const VMStateDescription vmstate_cpr_vfio_devices = {
    .name = CPR_STATE "/vfio devices",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_END_OF_LIST()
    }
};
