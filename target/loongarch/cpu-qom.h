/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU QOM header (target agnostic)
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_QOM_H
#define LOONGARCH_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_LOONGARCH_CPU "loongarch-cpu"
#define TYPE_LOONGARCH32_CPU "loongarch32-cpu"
#define TYPE_LOONGARCH64_CPU "loongarch64-cpu"

OBJECT_DECLARE_CPU_TYPE(LoongArchCPU, LoongArchCPUClass,
                        LOONGARCH_CPU)

#define LOONGARCH_CPU_TYPE_SUFFIX "-" TYPE_LOONGARCH_CPU
#define LOONGARCH_CPU_TYPE_NAME(model) model LOONGARCH_CPU_TYPE_SUFFIX

#endif
