/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 3A5000 ext interrupt controller definitions
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_EXTIOI_H
#define LOONGARCH_EXTIOI_H

#include "hw/intc/loongarch_extioi_common.h"

#define TYPE_LOONGARCH_EXTIOI "loongarch.extioi"
OBJECT_DECLARE_TYPE(LoongArchExtIOIState, LoongArchExtIOIClass, LOONGARCH_EXTIOI)

struct LoongArchExtIOIState {
    LoongArchExtIOICommonState parent_obj;
    int dev_fd;
};

struct LoongArchExtIOIClass {
    LoongArchExtIOICommonClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

void kvm_extioi_realize(DeviceState *dev, Error **errp);
int kvm_extioi_get(void *opaque);
int kvm_extioi_put(void *opaque, int version_id);

#endif /* LOONGARCH_EXTIOI_H */
