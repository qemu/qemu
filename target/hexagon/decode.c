/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "qemu/log.h"
#include "iclass.h"
#include "opcodes.h"
#include "genptr.h"
#include "decode.h"
#include "insn.h"
#include "printinsn.h"
#include "mmvec/decode_ext_mmvec.h"

#define fZXTN(N, M, VAL) ((VAL) & ((1LL << (N)) - 1))

enum {
    EXT_IDX_noext = 0,
    EXT_IDX_noext_AFTER = 4,
    EXT_IDX_mmvec = 4,
    EXT_IDX_mmvec_AFTER = 8,
    XX_LAST_EXT_IDX
};

#define DEF_REGMAP(NAME, ELEMENTS, ...) \
    static const unsigned int DECODE_REGISTER_##NAME[ELEMENTS] = \
    { __VA_ARGS__ };
#include "regmap.h"

#define DECODE_MAPPED_REG(REGNO, NAME) \
    insn->regno[REGNO] = DECODE_REGISTER_##NAME[insn->regno[REGNO]];

typedef struct {
    struct _dectree_table_struct *table_link;
    struct _dectree_table_struct *table_link_b;
    opcode_t opcode;
    enum {
        DECTREE_ENTRY_INVALID,
        DECTREE_TABLE_LINK,
        DECTREE_SUBINSNS,
        DECTREE_EXTSPACE,
        DECTREE_TERMINAL
    } type;
} dectree_entry_t;

typedef struct _dectree_table_struct {
    unsigned int (*lookup_function)(int startbit, int width, size4u_t opcode);
    unsigned int size;
    unsigned int startbit;
    unsigned int width;
    dectree_entry_t table[];
} dectree_table_t;

#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT) \
    static struct _dectree_table_struct dectree_table_##TAG;
#define TABLE_LINK(TABLE)                     /* NOTHING */
#define TERMINAL(TAG, ENC)                    /* NOTHING */
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC)    /* NOTHING */
#define EXTSPACE(TAG, ENC)                    /* NOTHING */
#define INVALID()                             /* NOTHING */
#define DECODE_END_TABLE(...)                 /* NOTHING */
#define DECODE_MATCH_INFO(...)                /* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)         /* NOTHING */
#define DECODE_OPINFO(...)                    /* NOTHING */

#include "dectree_generated.h"

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_SEPARATOR_BITS

#define DECODE_SEPARATOR_BITS(START, WIDTH) NULL, START, WIDTH
#define DECODE_NEW_TABLE_HELPER(TAG, SIZE, FN, START, WIDTH) \
    static dectree_table_t dectree_table_##TAG = { \
        .size = SIZE, \
        .lookup_function = FN, \
        .startbit = START, \
        .width = WIDTH, \
        .table = {
#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT) \
    DECODE_NEW_TABLE_HELPER(TAG, SIZE, WHATNOT)

#define TABLE_LINK(TABLE) \
    { .type = DECTREE_TABLE_LINK, .table_link = &dectree_table_##TABLE },
#define TERMINAL(TAG, ENC) \
    { .type = DECTREE_TERMINAL, .opcode = TAG  },
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC) \
    { \
        .type = DECTREE_SUBINSNS, \
        .table_link = &dectree_table_DECODE_SUBINSN_##CLASSA, \
        .table_link_b = &dectree_table_DECODE_SUBINSN_##CLASSB \
    },
#define EXTSPACE(TAG, ENC) { .type = DECTREE_EXTSPACE },
#define INVALID() { .type = DECTREE_ENTRY_INVALID, .opcode = XX_LAST_OPCODE },

#define DECODE_END_TABLE(...) } };

#define DECODE_MATCH_INFO(...)                /* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)         /* NOTHING */
#define DECODE_OPINFO(...)                    /* NOTHING */

#include "dectree_generated.h"

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_NEW_TABLE_HELPER
#undef DECODE_SEPARATOR_BITS

static dectree_table_t dectree_table_DECODE_EXT_EXT_noext = {
    .size = 1, .lookup_function = NULL, .startbit = 0, .width = 0,
    .table = {
        { .type = DECTREE_ENTRY_INVALID, .opcode = XX_LAST_OPCODE },
    }
};

static dectree_table_t *ext_trees[XX_LAST_EXT_IDX];

static void decode_ext_init(void)
{
    int i;
    for (i = EXT_IDX_noext; i < EXT_IDX_noext_AFTER; i++) {
        ext_trees[i] = &dectree_table_DECODE_EXT_EXT_noext;
    }
    for (i = EXT_IDX_mmvec; i < EXT_IDX_mmvec_AFTER; i++) {
        ext_trees[i] = &dectree_table_DECODE_EXT_EXT_mmvec;
    }
}

typedef struct {
    size4u_t mask;
    size4u_t match;
} decode_itable_entry_t;

#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT)  /* NOTHING */
#define TABLE_LINK(TABLE)                     /* NOTHING */
#define TERMINAL(TAG, ENC)                    /* NOTHING */
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC)    /* NOTHING */
#define EXTSPACE(TAG, ENC)                    /* NOTHING */
#define INVALID()                             /* NOTHING */
#define DECODE_END_TABLE(...)                 /* NOTHING */
#define DECODE_OPINFO(...)                    /* NOTHING */

#define DECODE_MATCH_INFO_NORMAL(TAG, MASK, MATCH) \
    [TAG] = { \
        .mask = MASK, \
        .match = MATCH, \
    },

#define DECODE_MATCH_INFO_NULL(TAG, MASK, MATCH) \
    [TAG] = { .match = ~0 },

#define DECODE_MATCH_INFO(...) DECODE_MATCH_INFO_NORMAL(__VA_ARGS__)
#define DECODE_LEGACY_MATCH_INFO(...) /* NOTHING */

static const decode_itable_entry_t decode_itable[XX_LAST_OPCODE] = {
#include "dectree_generated.h"
};

#undef DECODE_MATCH_INFO
#define DECODE_MATCH_INFO(...) DECODE_MATCH_INFO_NULL(__VA_ARGS__)

#undef DECODE_LEGACY_MATCH_INFO
#define DECODE_LEGACY_MATCH_INFO(...) DECODE_MATCH_INFO_NORMAL(__VA_ARGS__)

static const decode_itable_entry_t decode_legacy_itable[XX_LAST_OPCODE] = {
#include "dectree_generated.h"
};

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_SEPARATOR_BITS

void decode_init(void)
{
    decode_ext_init();
}

void decode_send_insn_to(packet_t *packet, int start, int newloc)
{
    insn_t tmpinsn;
    int direction;
    int i;
    if (start == newloc) {
        return;
    }
    if (start < newloc) {
        /* Move towards end */
        direction = 1;
    } else {
        /* move towards beginning */
        direction = -1;
    }
    for (i = start; i != newloc; i += direction) {
        tmpinsn = packet->insn[i];
        packet->insn[i] = packet->insn[i + direction];
        packet->insn[i + direction] = tmpinsn;
    }
}

/* Fill newvalue registers with the correct regno */
static int
decode_fill_newvalue_regno(packet_t *packet)
{
    int i, def_regnum, use_regidx, def_idx;
    size2u_t def_opcode, use_opcode;
    char *dststr;

    for (i = 1; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE) &&
            !GET_ATTRIB(packet->insn[i].opcode, A_EXTENSION)) {
            use_opcode = packet->insn[i].opcode;

            /* It's a store, so we're adjusting the Nt field */
            if (GET_ATTRIB(use_opcode, A_STORE)) {
                use_regidx = strchr(opcode_reginfo[use_opcode], 't') -
                    opcode_reginfo[use_opcode];
            } else {    /* It's a Jump, so we're adjusting the Ns field */
                use_regidx = strchr(opcode_reginfo[use_opcode], 's') -
                    opcode_reginfo[use_opcode];
            }

            /*
             * What's encoded at the N-field is the offset to who's producing
             * the value.  Shift off the LSB which indicates odd/even register.
             */
            def_idx = i - ((packet->insn[i].regno[use_regidx]) >> 1);

            /*
             * Check for a badly encoded N-field which points to an instruction
             * out-of-range
             */
            if ((def_idx < 0) || (def_idx > (packet->num_insns - 1))) {
                g_assert_not_reached();
                return 1;
            }

            /* previous insn is the producer */
            def_opcode = packet->insn[def_idx].opcode;
            dststr = strstr(opcode_wregs[def_opcode], "Rd");
            if (dststr) {
                dststr = strchr(opcode_reginfo[def_opcode], 'd');
            } else {
                dststr = strstr(opcode_wregs[def_opcode], "Rx");
                if (dststr) {
                    dststr = strchr(opcode_reginfo[def_opcode], 'x');
                } else {
                    dststr = strstr(opcode_wregs[def_opcode], "Re");
                    if (dststr) {
                        dststr = strchr(opcode_reginfo[def_opcode], 'e');
                    } else {
                        dststr = strstr(opcode_wregs[def_opcode], "Ry");
                        if (dststr) {
                            dststr = strchr(opcode_reginfo[def_opcode], 'y');
                        } else {
                            g_assert_not_reached();
                            return 1;
                        }
                    }
                }
            }
            g_assert(dststr != NULL);
            def_regnum =
                packet->insn[def_idx].regno[dststr -
                    opcode_reginfo[def_opcode]];

            /* Now patch up the consumer with the register number */
            packet->insn[i].regno[use_regidx] = def_regnum;
            /*
             * We need to remember who produces this value to later
             * check if it was dynamically cancelled
             */
            packet->insn[i].new_value_producer_slot =
                packet->insn[def_idx].slot;
        }
    }
    return 0;
}

/* Split CJ into a compare and a jump */
static int decode_split_cmpjump(packet_t *pkt)
{
    int last, i;
    int numinsns = pkt->num_insns;

    /*
     * First, split all compare-jumps.
     * The compare is sent to the end as a new instruction.
     * Do it this way so we don't reorder dual jumps. Those need to stay in
     * original order.
     */
    for (i = 0; i < numinsns; i++) {
        /* It's a cmp-jump */
        if (GET_ATTRIB(pkt->insn[i].opcode, A_NEWCMPJUMP)) {
            last = pkt->num_insns;
            pkt->insn[last] = pkt->insn[i];    /* copy the instruction */
            pkt->insn[last].part1 = 1;    /* last instruction does the CMP */
            pkt->insn[i].part1 = 0;    /* existing instruction does the JUMP */
        pkt->num_insns++;
        }
    }

    /* Now re-shuffle all the compares back to the beginning */
    for (i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            decode_send_insn_to(pkt, i, 0);
        }
    }
    return 0;
}

static inline int decode_opcode_can_jump(int opcode)
{
    if ((GET_ATTRIB(opcode, A_JUMP)) ||
        (GET_ATTRIB(opcode, A_CALL)) ||
        (opcode == J2_trap0) ||
        (opcode == J2_trap1) ||
        (opcode == J2_rte) ||
        (opcode == J2_pause)) {
        /* Exception to A_JUMP attribute */
        if (opcode == J4_hintjumpr) {
            return 0;
        }
        return 1;
    }

    return 0;
}

static inline int decode_opcode_ends_loop(int opcode)
{
    return GET_ATTRIB(opcode, A_HWLOOP0_END) ||
           GET_ATTRIB(opcode, A_HWLOOP1_END);
}

/* Set the is_* fields in each instruction */
static int decode_set_insn_attr_fields(packet_t *pkt)
{
    int i;
    int numinsns = pkt->num_insns;
    size2u_t opcode;
    int loads = 0;
    int stores = 0;
    int canjump;
    int total_slots_valid = 0;

    pkt->num_rops = 0;
    pkt->pkt_has_cof = 0;
    pkt->pkt_has_call = 0;
    pkt->pkt_has_jumpr = 0;
    pkt->pkt_has_cjump = 0;
    pkt->pkt_has_cjump_dotnew = 0;
    pkt->pkt_has_cjump_dotold = 0;
    pkt->pkt_has_cjump_newval = 0;
    pkt->pkt_has_endloop = 0;
    pkt->pkt_has_endloop0 = 0;
    pkt->pkt_has_endloop01 = 0;
    pkt->pkt_has_endloop1 = 0;
    pkt->pkt_has_cacheop = 0;
    pkt->memop_or_nvstore = 0;
    pkt->pkt_has_dczeroa = 0;
    pkt->pkt_has_dealloc_return = 0;

    for (i = 0; i < numinsns; i++) {
        opcode = pkt->insn[i].opcode;
        if (pkt->insn[i].part1) {
            continue;    /* Skip compare of cmp-jumps */
        }

        if (GET_ATTRIB(opcode, A_ROPS_3)) {
            pkt->num_rops += 3;
        } else if (GET_ATTRIB(opcode, A_ROPS_2)) {
            pkt->num_rops += 2;
        } else {
            pkt->num_rops++;
        }
        if (pkt->insn[i].extension_valid) {
            pkt->num_rops += 2;
        }

        if (GET_ATTRIB(opcode, A_MEMOP) ||
            GET_ATTRIB(opcode, A_NVSTORE)) {
            pkt->memop_or_nvstore = 1;
        }

        if (GET_ATTRIB(opcode, A_CACHEOP)) {
            pkt->pkt_has_cacheop = 1;
            if (GET_ATTRIB(opcode, A_DCZEROA)) {
                pkt->pkt_has_dczeroa = 1;
            }
            if (GET_ATTRIB(opcode, A_ICTAGOP)) {
                pkt->pkt_has_ictagop = 1;
            }
            if (GET_ATTRIB(opcode, A_ICFLUSHOP)) {
                pkt->pkt_has_icflushop = 1;
            }
            if (GET_ATTRIB(opcode, A_DCTAGOP)) {
                pkt->pkt_has_dctagop = 1;
            }
            if (GET_ATTRIB(opcode, A_DCFLUSHOP)) {
                pkt->pkt_has_dcflushop = 1;
            }
            if (GET_ATTRIB(opcode, A_L2TAGOP)) {
                pkt->pkt_has_l2tagop = 1;
            }
            if (GET_ATTRIB(opcode, A_L2FLUSHOP)) {
                pkt->pkt_has_l2flushop = 1;
            }
        }

        if (GET_ATTRIB(opcode, A_DEALLOCRET)) {
            pkt->pkt_has_dealloc_return = 1;
        }

        if (GET_ATTRIB(opcode, A_STORE)) {
            pkt->insn[i].is_store = 1;
            if (GET_ATTRIB(opcode, A_VMEM)) {
                pkt->insn[i].is_vmem_st = 1;
            }

            if (pkt->insn[i].slot == 0) {
                pkt->pkt_has_store_s0 = 1;
            } else {
                pkt->pkt_has_store_s1 = 1;
            }
        }
        if (GET_ATTRIB(opcode, A_DCFETCH)) {
            pkt->insn[i].is_dcfetch = 1;
        }
        if (GET_ATTRIB(opcode, A_LOAD)) {
            pkt->insn[i].is_load = 1;
            if (GET_ATTRIB(opcode, A_VMEM))  {
                pkt->insn[i].is_vmem_ld = 1;
            }

            if (pkt->insn[i].slot == 0) {
                pkt->pkt_has_load_s0 = 1;
            } else {
                pkt->pkt_has_load_s1 = 1;
            }
        }
        if (GET_ATTRIB(opcode, A_CVI_GATHER) ||
            GET_ATTRIB(opcode, A_CVI_SCATTER)) {
            pkt->insn[i].is_scatgath = 1;
        }
        if (GET_ATTRIB(opcode, A_MEMOP)) {
            pkt->insn[i].is_memop = 1;
        }
        if (GET_ATTRIB(opcode, A_DEALLOCRET) ||
            GET_ATTRIB(opcode, A_DEALLOCFRAME)) {
            pkt->insn[i].is_dealloc = 1;
        }
        if (GET_ATTRIB(opcode, A_DCFLUSHOP) ||
            GET_ATTRIB(opcode, A_DCTAGOP)) {
            pkt->insn[i].is_dcop = 1;
        }

        pkt->pkt_has_call |= GET_ATTRIB(opcode, A_CALL);
        pkt->pkt_has_jumpr |= GET_ATTRIB(opcode, A_INDIRECT) &&
                              !GET_ATTRIB(opcode, A_HINTJR);
        pkt->pkt_has_cjump |= GET_ATTRIB(opcode, A_CJUMP);
        pkt->pkt_has_cjump_dotnew |= GET_ATTRIB(opcode, A_DOTNEW) &&
                                     GET_ATTRIB(opcode, A_CJUMP);
        pkt->pkt_has_cjump_dotold |= GET_ATTRIB(opcode, A_DOTOLD) &&
                                     GET_ATTRIB(opcode, A_CJUMP);
        pkt->pkt_has_cjump_newval |= GET_ATTRIB(opcode, A_DOTNEWVALUE) &&
                                     GET_ATTRIB(opcode, A_CJUMP);

        canjump = decode_opcode_can_jump(opcode);

        if (pkt->pkt_has_cof) {
            if (canjump) {
                pkt->pkt_has_dual_jump = 1;
                pkt->insn[i].is_2nd_jump = 1;
            }
        } else {
            pkt->pkt_has_cof |= canjump;
        }

        pkt->insn[i].is_endloop = decode_opcode_ends_loop(opcode);

        pkt->pkt_has_endloop |= pkt->insn[i].is_endloop;
        pkt->pkt_has_endloop0 |= GET_ATTRIB(opcode, A_HWLOOP0_END) &&
                                 !GET_ATTRIB(opcode, A_HWLOOP1_END);
        pkt->pkt_has_endloop01 |= GET_ATTRIB(opcode, A_HWLOOP0_END) &&
                                  GET_ATTRIB(opcode, A_HWLOOP1_END);
        pkt->pkt_has_endloop1 |= GET_ATTRIB(opcode, A_HWLOOP1_END) &&
                                 !GET_ATTRIB(opcode, A_HWLOOP0_END);

        pkt->pkt_has_cof |= pkt->pkt_has_endloop;

        /* Now create slot valids */
        if (pkt->insn[i].is_endloop)    /* Don't count endloops */
            continue;

        switch (pkt->insn[i].slot) {
        case 0:
            pkt->slot0_valid = 1;
            break;
        case 1:
            pkt->slot1_valid = 1;
            break;
        case 2:
            pkt->slot2_valid = 1;
            break;
        case 3:
            pkt->slot3_valid = 1;
        break;
        }
        total_slots_valid++;

        /* And track #loads/stores */
        if (pkt->insn[i].is_store) {
            stores++;
        } else if (pkt->insn[i].is_load) {
            loads++;
        }
    }

    if (stores == 2) {
        pkt->dual_store = 1;
    } else if (loads == 2) {
        pkt->dual_load = 1;
    } else if ((loads == 1) && (stores == 1)) {
        pkt->load_and_store = 1;
    } else if (loads == 1) {
        pkt->single_load = 1;
    } else if (stores == 1) {
        pkt->single_store = 1;
    }

    return 0;
}

/*
 * Shuffle for execution
 * Move stores to end (in same order as encoding)
 * Move compares to beginning (for use by .new insns)
 */
static int decode_shuffle_for_execution(packet_t *packet)
{
    int changed = 0;
    int i;
    int flag;    /* flag means we've seen a non-memory instruction */
    int n_mems;
    int last_insn = packet->num_insns - 1;

    /*
     * Skip end loops, somehow an end loop is getting in and messing
     * up the order
     */
    if (decode_opcode_ends_loop(packet->insn[last_insn].opcode)) {
        last_insn--;
    }

    do {
        changed = 0;
        /*
         * Stores go last, must not reorder.
         * Cannot shuffle stores past loads, either.
         * Iterate backwards.  If we see a non-memory instruction,
         * then a store, shuffle the store to the front.  Don't shuffle
         *  stores wrt each other or a load.
         */
        for (flag = n_mems = 0, i = last_insn; i >= 0; i--) {
            int opcode = packet->insn[i].opcode;

            if (flag && GET_ATTRIB(opcode, A_STORE)) {
                decode_send_insn_to(packet, i, last_insn - n_mems);
                n_mems++;
                changed = 1;
            } else if (GET_ATTRIB(opcode, A_STORE)) {
                n_mems++;
            } else if (GET_ATTRIB(opcode, A_LOAD)) {
                /*
                 * Don't set flag, since we don't want to shuffle a
                 * store pasta load
                 */
                n_mems++;
            } else if (GET_ATTRIB(opcode, A_DOTNEWVALUE)) {
                /*
                 * Don't set flag, since we don't want to shuffle past
                 * a .new value
                 */
            } else {
                flag = 1;
            }
        }

        if (changed) {
            continue;
        }
        /* Compares go first, may be reordered wrt each other */
        for (flag = 0, i = 0; i < last_insn + 1; i++) {
            int opcode = packet->insn[i].opcode;

            if ((strstr(opcode_wregs[opcode], "Pd4") ||
                 strstr(opcode_wregs[opcode], "Pe4")) &&
                GET_ATTRIB(opcode, A_STORE) == 0) {
                /* This should be a compare (not a store conditional) */
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = 1;
                    continue;
                }
            } else if (GET_ATTRIB(opcode, A_IMPLICIT_WRITES_P3) &&
                       !decode_opcode_ends_loop(packet->insn[i].opcode)) {
                /*
                 * spNloop instruction
                 * Don't reorder endloops; they are not valid for .new uses,
                 * and we want to match HW
                 */
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = 1;
                    continue;
                }
            } else if (GET_ATTRIB(opcode, A_IMPLICIT_WRITES_P0) &&
                       !GET_ATTRIB(opcode, A_NEWCMPJUMP)) {
                /* CABAC instruction */
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = 1;
                    continue;
                }
            } else {
                flag = 1;
            }
        }
        if (changed) {
            continue;
        }
    } while (changed);

    /*
     * If we have a .new register compare/branch, move that to the very
     * very end, past stores
     */
    for (i = 0; i < last_insn; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE)) {
            decode_send_insn_to(packet, i, last_insn);
            break;
        }
    }

    /*
     * And at the very very very end, move any RTE's, since they update
     * user/supervisor mode.
     */
    for (i = 0; i < last_insn; i++) {
        if ((packet->insn[i].opcode == J2_rte)) {
            decode_send_insn_to(packet, i, last_insn);
            break;
        }
    }
    return 0;
}

static void decode_assembler_count_fpops(packet_t *pkt)
{
    int i;
    for (i = 0; i < pkt->num_insns; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_FPOP)) {
            pkt->pkt_has_fp_op = 1;
        }
        if (GET_ATTRIB(pkt->insn[i].opcode, A_FPDOUBLE)) {
            pkt->pkt_has_fpdp_op = 1;
        } else if (GET_ATTRIB(pkt->insn[i].opcode, A_FPSINGLE)) {
            pkt->pkt_has_fpsp_op = 1;
        }
    }
}

static int
apply_extender(packet_t *pkt, int i, size4u_t extender)
{
    int immed_num;
    size4u_t base_immed;

    immed_num = opcode_which_immediate_is_extended(pkt->insn[i].opcode);
    base_immed = pkt->insn[i].immed[immed_num];

    pkt->insn[i].immed[immed_num] = extender | fZXTN(6, 32, base_immed);
    return 0;
}

static int decode_apply_extenders(packet_t *packet)
{
    int i;
    for (i = 0; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
            packet->insn[i + 1].extension_valid = 1;
            packet->pkt_has_payload = 1;
            apply_extender(packet, i + 1, packet->insn[i].immed[0]);
        }
    }
    return 0;
}

static int decode_remove_extenders(packet_t *packet)
{
    int i, j;
    for (i = 0; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
            for (j = i;
                (j < packet->num_insns - 1) && (j < INSTRUCTIONS_MAX - 1);
                j++) {
                packet->insn[j] = packet->insn[j + 1];
            }
            packet->num_insns--;
        }
    }
    return 0;
}

static const char *
get_valid_slot_str(const packet_t *pkt, unsigned int slot)
{
    if (GET_ATTRIB(pkt->insn[slot].opcode, A_EXTENSION)) {
        return mmvec_ext_decode_find_iclass_slots(pkt->insn[slot].opcode);
    } else {
        return find_iclass_slots(pkt->insn[slot].opcode,
                                 pkt->insn[slot].iclass);
    }
}

#include "q6v_decode.c"

packet_t *decode_this(int max_words, size4u_t *words, packet_t *decode_pkt)
{
    int ret;
    ret = do_decode_packet(max_words, words, decode_pkt);
    if (ret <= 0) {
        /* ERROR or BAD PARSE */
        return NULL;
    }
    return decode_pkt;
}

/* Used for "-d in_asm" logging */
int disassemble_hexagon(uint32_t *words, int nwords, char *buf, int bufsize)
{
    packet_t pkt;

    if (decode_this(nwords, words, &pkt)) {
        snprint_a_pkt(buf, bufsize, &pkt);
        return pkt.encod_pkt_size_in_bytes;
    } else {
        snprintf(buf, bufsize, "<invalid>");
        return 0;
    }
}
