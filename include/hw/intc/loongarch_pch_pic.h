/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 7A1000 I/O interrupt controller definitions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_PCH_PIC_H
#define HW_LOONGARCH_PCH_PIC_H

#include "hw/intc/loongarch_pic_common.h"

#define LoongArchPCHPIC LoongArchPICCommonState
#define TYPE_LOONGARCH_PCH_PIC "loongarch_pch_pic"
#define PCH_PIC_NAME(name) TYPE_LOONGARCH_PCH_PIC#name
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchPCHPIC, LOONGARCH_PCH_PIC)

#endif /* HW_LOONGARCH_PCH_PIC_H */
