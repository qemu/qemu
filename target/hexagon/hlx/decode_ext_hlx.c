/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "decode.h"
#include "opcodes.h"
#include "insn.h"
#include "iclass.h"
#include "hlx/hlx.h"
#include "hlx/decode_ext_hlx.h"

typedef struct {
    bool store;
    bool load;
    bool shift;
    bool mpy;
} hlx_pkt_resources_t;

static bool check_hlx_slot_constraints(Packet *pkt, int slot, int insn_idx,
                                       hlx_pkt_resources_t *resources)
{
    int opcode = pkt->insn[insn_idx].opcode;

    if (slot == 0) {
        if (!GET_ATTRIB(opcode, A_STORE)) {
            return false;
        }
        if (resources->store) {
            return false;
        }
        resources->store = true;
    } else if (slot == 1) {
        if (!GET_ATTRIB(opcode, A_LOAD)) {
            return false;
        }
        if (resources->load) {
            return false;
        }
        resources->load = true;
    } else if (slot == 2) {
        if (!GET_ATTRIB(opcode, A_HLX_SHIFT) &&
            !GET_ATTRIB(opcode, A_HLX_COMMON_ALU)) {
            return false;
        }
        if (resources->shift) {
            return false;
        }
        resources->shift = true;
    } else if (slot == 3) {
        if (!GET_ATTRIB(opcode, A_HLX_MPY) &&
            !GET_ATTRIB(opcode, A_HLX_COMMON_ALU)) {
            return false;
        }
        if (resources->mpy) {
            return false;
        }
        resources->mpy = true;
    }

    return true;
}

bool hlx_ext_decode_checks(Packet *pkt, bool disas_only)
{
    hlx_pkt_resources_t resources = {
        .store = false,
        .load = false,
        .shift = false,
        .mpy = false
    };

    /* Slots are assigned only for execution. */
    if (disas_only) {
        return true;
    }

    for (int i = 0; i < pkt->num_insns; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_HLX) &&
            !check_hlx_slot_constraints(pkt, pkt->insn[i].slot, i,
                                        &resources)) {
            return false;
        }
    }
    return true;
}

SlotMask hlx_ext_decode_find_iclass_slots(int opcode)
{
    if (GET_ATTRIB(opcode, A_RESTRICT_SLOT0ONLY) ||
        GET_ATTRIB(opcode, A_STORE)) {
        return SLOTS_0;
    }
    if (GET_ATTRIB(opcode, A_RESTRICT_SLOT1ONLY) ||
        GET_ATTRIB(opcode, A_LOAD)) {
        return SLOTS_1;
    }
    if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY) ||
        GET_ATTRIB(opcode, A_HLX_SHIFT)) {
        return SLOTS_2;
    }
    if (GET_ATTRIB(opcode, A_RESTRICT_SLOT3ONLY) ||
        GET_ATTRIB(opcode, A_HLX_MPY)) {
        return SLOTS_3;
    }
    if (GET_ATTRIB(opcode, A_HLX_COMMON_ALU) ||
        GET_ATTRIB(opcode, A_HLX_SLOT23)) {
        return SLOTS_23;
    }
    if (GET_ATTRIB(opcode, A_HLX_ANY_RESOURCE)) {
        return SLOTS_0123;
    }

    g_assert_not_reached();
    return SLOTS_0123;
}
