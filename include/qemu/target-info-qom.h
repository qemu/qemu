/*
 * QEMU target info QOM types
 *
 * Copyright (c) Qualcomm
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_QOM_H
#define QEMU_TARGET_INFO_QOM_H

#include "qemu/target-info-impl.h"
#include "qom/object.h"

#define TYPE_TARGET_INFO "target-info"

typedef struct TargetInfoQom {
    Object parent_obj;
} TargetInfoQom;

typedef struct TargetInfoQomClass {
    ObjectClass parent_class;
    const TargetInfo *target_info;
} TargetInfoQomClass;

OBJECT_DECLARE_TYPE(TargetInfoQom, TargetInfoQomClass, TARGET_INFO)

void target_info_qom_set_target(void);
void target_info_qom_set_target_info(const TargetInfo *ti);

#endif /* QEMU_TARGET_INFO_QOM_H */
