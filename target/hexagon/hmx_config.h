/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HMX_CONFIG_H
#define HEXAGON_HMX_CONFIG_H

#include "cpu-qom.h"

typedef struct HmxConfig {
    bool hmx_present;
} HmxConfig;

void hmx_init_config(HexagonCPU *cpu);

#endif /* HEXAGON_HMX_CONFIG_H */
