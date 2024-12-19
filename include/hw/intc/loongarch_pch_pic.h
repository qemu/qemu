/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 7A1000 I/O interrupt controller definitions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_PCH_PIC_H
#define HW_LOONGARCH_PCH_PIC_H

#include "hw/intc/loongarch_pic_common.h"

#define TYPE_LOONGARCH_PIC  "loongarch_pic"
#define PCH_PIC_NAME(name)  TYPE_LOONGARCH_PIC#name
OBJECT_DECLARE_TYPE(LoongarchPICState, LoongarchPICClass, LOONGARCH_PIC)

struct LoongarchPICState {
    LoongArchPICCommonState parent_obj;
};

struct LoongarchPICClass {
    LoongArchPICCommonClass parent_class;

    DeviceRealize parent_realize;
};

#endif /* HW_LOONGARCH_PCH_PIC_H */
