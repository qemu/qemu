/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt header files
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_IPI_H
#define HW_LOONGARCH_IPI_H

#include "qom/object.h"
#include "hw/intc/loongson_ipi_common.h"

#define TYPE_LOONGARCH_IPI  "loongarch_ipi"
OBJECT_DECLARE_TYPE(LoongarchIPIState, LoongarchIPIClass, LOONGARCH_IPI)

struct LoongarchIPIState {
    LoongsonIPICommonState parent_obj;
};

struct LoongarchIPIClass {
    LoongsonIPICommonClass parent_class;
};

#endif
