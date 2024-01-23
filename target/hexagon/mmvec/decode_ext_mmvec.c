/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "decode.h"
#include "opcodes.h"
#include "insn.h"
#include "iclass.h"
#include "mmvec/mmvec.h"
#include "mmvec/decode_ext_mmvec.h"

static void
check_new_value(Packet *pkt)
{
    /* .new value for a MMVector store */
    int i, j;
    const char *reginfo;
    const char *destletters;
    const char *dststr = NULL;
    uint16_t def_opcode;
    char letter;

    for (i = 1; i < pkt->num_insns; i++) {
        uint16_t use_opcode = pkt->insn[i].opcode;
        if (GET_ATTRIB(use_opcode, A_DOTNEWVALUE) &&
            GET_ATTRIB(use_opcode, A_CVI) &&
            GET_ATTRIB(use_opcode, A_STORE)) {
            int use_regidx = strchr(opcode_reginfo[use_opcode], 's') -
                opcode_reginfo[use_opcode];
            /*
             * What's encoded at the N-field is the offset to who's producing
             * the value.
             * Shift off the LSB which indicates odd/even register.
             */
            int def_off = ((pkt->insn[i].regno[use_regidx]) >> 1);
            int def_oreg = pkt->insn[i].regno[use_regidx] & 1;
            int def_idx = -1;
            for (j = i - 1; (j >= 0) && (def_off >= 0); j--) {
                if (!GET_ATTRIB(pkt->insn[j].opcode, A_CVI)) {
                    continue;
                }
                def_off--;
                if (def_off == 0) {
                    def_idx = j;
                    break;
                }
            }
            /*
             * Check for a badly encoded N-field which points to an instruction
             * out-of-range
             */
            g_assert(!((def_off != 0) || (def_idx < 0) ||
                       (def_idx > (pkt->num_insns - 1))));

            /* def_idx is the index of the producer */
            def_opcode = pkt->insn[def_idx].opcode;
            reginfo = opcode_reginfo[def_opcode];
            destletters = "dexy";
            for (j = 0; (letter = destletters[j]) != 0; j++) {
                dststr = strchr(reginfo, letter);
                if (dststr != NULL) {
                    break;
                }
            }
            if ((dststr == NULL)  && GET_ATTRIB(def_opcode, A_CVI_GATHER)) {
                pkt->insn[i].regno[use_regidx] = def_oreg;
                pkt->insn[i].new_value_producer_slot = pkt->insn[def_idx].slot;
            } else {
                if (dststr == NULL) {
                    /* still not there, we have a bad packet */
                    g_assert_not_reached();
                }
                int def_regnum = pkt->insn[def_idx].regno[dststr - reginfo];
                /* Now patch up the consumer with the register number */
                pkt->insn[i].regno[use_regidx] = def_regnum ^ def_oreg;
                /* special case for (Vx,Vy) */
                dststr = strchr(reginfo, 'y');
                if (def_oreg && strchr(reginfo, 'x') && dststr) {
                    def_regnum = pkt->insn[def_idx].regno[dststr - reginfo];
                    pkt->insn[i].regno[use_regidx] = def_regnum;
                }
                /*
                 * We need to remember who produces this value to later
                 * check if it was dynamically cancelled
                 */
                pkt->insn[i].new_value_producer_slot = pkt->insn[def_idx].slot;
            }
        }
    }
}

/*
 * We don't want to reorder slot1/slot0 with respect to each other.
 * So in our shuffling, we don't want to move the .cur / .tmp vmem earlier
 * Instead, we should move the producing instruction later
 * But the producing instruction might feed a .new store!
 * So we may need to move that even later.
 */

static void
decode_mmvec_move_cvi_to_end(Packet *pkt, int max)
{
    int i;
    for (i = 0; i < max; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_CVI)) {
            int last_inst = pkt->num_insns - 1;
            uint16_t last_opcode = pkt->insn[last_inst].opcode;

            /*
             * If the last instruction is an endloop, move to the one before it
             * Keep endloop as the last thing always
             */
            if ((last_opcode == J2_endloop0) ||
                (last_opcode == J2_endloop1) ||
                (last_opcode == J2_endloop01)) {
                last_inst--;
            }

            decode_send_insn_to(pkt, i, last_inst);
            max--;
            i--;    /* Retry this index now that packet has rotated */
        }
    }
}

static void
decode_shuffle_for_execution_vops(Packet *pkt)
{
    /*
     * Sort for .new
     */
    int i;
    for (i = 0; i < pkt->num_insns; i++) {
        uint16_t opcode = pkt->insn[i].opcode;
        if ((GET_ATTRIB(opcode, A_LOAD) &&
             GET_ATTRIB(opcode, A_CVI_NEW)) ||
            GET_ATTRIB(opcode, A_CVI_TMP)) {
            /*
             * Find prior consuming vector instructions
             * Move to end of packet
             */
            decode_mmvec_move_cvi_to_end(pkt, i);
            break;
        }
    }

    /* Move HVX new value stores to the end of the packet */
    for (i = 0; i < pkt->num_insns - 1; i++) {
        uint16_t opcode = pkt->insn[i].opcode;
        if (GET_ATTRIB(opcode, A_STORE) &&
            GET_ATTRIB(opcode, A_CVI_NEW) &&
            !GET_ATTRIB(opcode, A_CVI_SCATTER_RELEASE)) {
            int last_inst = pkt->num_insns - 1;
            uint16_t last_opcode = pkt->insn[last_inst].opcode;

            /*
             * If the last instruction is an endloop, move to the one before it
             * Keep endloop as the last thing always
             */
            if ((last_opcode == J2_endloop0) ||
                (last_opcode == J2_endloop1) ||
                (last_opcode == J2_endloop01)) {
                last_inst--;
            }

            decode_send_insn_to(pkt, i, last_inst);
            break;
        }
    }
}

static void
check_for_vhist(Packet *pkt)
{
    pkt->vhist_insn = NULL;
    for (int i = 0; i < pkt->num_insns; i++) {
        Insn *insn = &pkt->insn[i];
        int opcode = insn->opcode;
        if (GET_ATTRIB(opcode, A_CVI) && GET_ATTRIB(opcode, A_CVI_4SLOT)) {
                pkt->vhist_insn = insn;
                return;
        }
    }
}

/*
 * Public Functions
 */

SlotMask mmvec_ext_decode_find_iclass_slots(int opcode)
{
    if (GET_ATTRIB(opcode, A_CVI_VM)) {
        /* HVX memory instruction */
        if (GET_ATTRIB(opcode, A_RESTRICT_SLOT0ONLY)) {
            return SLOTS_0;
        } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT1ONLY)) {
            return SLOTS_1;
        }
        return SLOTS_01;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
        return SLOTS_2;
    } else if (GET_ATTRIB(opcode, A_CVI_VX)) {
        /* HVX multiply instruction */
        return SLOTS_23;
    } else if (GET_ATTRIB(opcode, A_CVI_VS_VX)) {
        /* HVX permute/shift instruction */
        return SLOTS_23;
    } else {
        return SLOTS_0123;
    }
}

void mmvec_ext_decode_checks(Packet *pkt, bool disas_only)
{
    check_new_value(pkt);
    if (!disas_only) {
        decode_shuffle_for_execution_vops(pkt);
    }
    check_for_vhist(pkt);
}
