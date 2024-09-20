/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 3A5000 ext interrupt controller definitions
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_EXTIOI_H
#define LOONGARCH_EXTIOI_H

#include "hw/intc/loongarch_extioi_common.h"

#define LoongArchExtIOI LoongArchExtIOICommonState
#define TYPE_LOONGARCH_EXTIOI "loongarch.extioi"
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchExtIOI, LOONGARCH_EXTIOI)
#endif /* LOONGARCH_EXTIOI_H */
