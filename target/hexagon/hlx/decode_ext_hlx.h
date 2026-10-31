/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_DECODE_EXT_HLX_H
#define HEXAGON_DECODE_EXT_HLX_H

#include "insn.h"

bool hlx_ext_decode_checks(Packet *pkt, bool disas_only);
SlotMask hlx_ext_decode_find_iclass_slots(int opcode);

#endif /* HEXAGON_DECODE_EXT_HLX_H */
