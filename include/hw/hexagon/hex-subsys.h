/*
 * Hexagon subsystem helpers shared between the machine models.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_HEX_SUBSYS_H
#define HW_HEXAGON_HEX_SUBSYS_H

#include "hw/hexagon/hexagon.h"

/* Create the subsystem shared by every Hexagon machine. */
void hex_subsys_create(HexagonCommonMachineState *hms,
                       const struct hexagon_machine_config *m_cfg);

#endif /* HW_HEXAGON_HEX_SUBSYS_H */
