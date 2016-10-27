/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <migration/colo.h>
#include "trace.h"

typedef struct {
     bool colo_requested;
} COLOInfo;

static COLOInfo colo_info;

static void colo_info_pre_save(void *opaque)
{
    COLOInfo *s = opaque;

    s->colo_requested = migrate_colo_enabled();
}

static bool colo_info_need(void *opaque)
{
   return migrate_colo_enabled();
}

static const VMStateDescription colo_state = {
    .name = "COLOState",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = colo_info_pre_save,
    .needed = colo_info_need,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(colo_requested, COLOInfo),
        VMSTATE_END_OF_LIST()
    },
};

void colo_info_init(void)
{
    vmstate_register(NULL, 0, &colo_state, &colo_info);
}
