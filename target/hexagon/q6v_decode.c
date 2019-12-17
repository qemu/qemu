/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT)     /* NOTHING */
#define TABLE_LINK(TABLE)                        /* NOTHING */
#define TERMINAL(TAG, ENC)                       /* NOTHING */
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC)       /* NOTHING */
#define EXTSPACE(TAG, ENC)                       /* NOTHING */
#define INVALID()                                /* NOTHING */
#define DECODE_END_TABLE(...)                    /* NOTHING */
#define DECODE_MATCH_INFO(...)                   /* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)            /* NOTHING */

#define DECODE_REG(REGNO, WIDTH, STARTBIT) \
    insn->regno[REGNO] = ((encoding >> STARTBIT) & ((1 << WIDTH) - 1));

#define DECODE_IMPL_REG(REGNO, VAL) \
    insn->regno[REGNO] = VAL;

#define DECODE_IMM(IMMNO, WIDTH, STARTBIT, VALSTART) \
    insn->immed[IMMNO] |= (((encoding >> STARTBIT) & ((1 << WIDTH) - 1))) << \
                          (VALSTART);

#define DECODE_IMM_SXT(IMMNO, WIDTH) \
    insn->immed[IMMNO] = ((((size4s_t)insn->immed[IMMNO]) << (32 - WIDTH)) >> \
                          (32 - WIDTH));

#define DECODE_IMM_NEG(IMMNO, WIDTH) \
    insn->immed[IMMNO] = -insn->immed[IMMNO];

#define DECODE_IMM_SHIFT(IMMNO, SHAMT)                                 \
    if ((!insn->extension_valid) || \
        (insn->which_extended != IMMNO)) { \
        insn->immed[IMMNO] <<= SHAMT; \
    }

#define DECODE_OPINFO(TAG, BEH) \
    case TAG: \
        { BEH  } \
        break; \

static void
decode_op(insn_t *insn, opcode_t tag, size4u_t encoding)
{
    insn->immed[0] = 0;
    insn->immed[1] = 0;
    if (insn->extension_valid) {
        insn->which_extended = opcode_which_immediate_is_extended(tag);
    }
    insn->opcode = tag;

    switch (tag) {
#include "imported/dectree.odef"
    default:
        break;
    }

    insn->generate = opcode_genptr[tag];
    insn->iclass = (encoding >> 28) & 0xf;
    if (((encoding >> 14) & 3) == 0) {
        insn->iclass += 16;
    }
}

#undef DECODE_REG
#undef DECODE_IMPL_REG
#undef DECODE_IMM
#undef DECODE_IMM_SHIFT
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

static unsigned int
decode_subinsn_tablewalk(insn_t *insn, dectree_table_t *table,
                         size4u_t encoding)
{
    unsigned int i;
    opcode_t opc;
    if (table->lookup_function) {
        i = table->lookup_function(table->startbit, table->width, encoding);
    } else {
        i = ((encoding >> table->startbit) & ((1 << table->width) - 1));
    }
    if (table->table[i].type == DECTREE_TABLE_LINK) {
        return decode_subinsn_tablewalk(insn, table->table[i].table_link,
                                        encoding);
    } else if (table->table[i].type == DECTREE_SUBINSNS) {
        fatal("No sub-sub instructions");
        return 0;
    } else if (table->table[i].type == DECTREE_TERMINAL) {
        opc = table->table[i].opcode;
        if ((encoding & decode_itable[opc].mask) != decode_itable[opc].match) {
            return 0;
        }
        decode_op(insn, opc, encoding);
        return 1;
    } else if (table->table[i].type == DECTREE_EXTSPACE) {
        fatal("no extension subinsns");
        return 0;
    } else {
        return 0;
    }
}

static unsigned int get_insn_a(size4u_t encoding)
{
    return encoding & 0x00001fff;
}

static unsigned int get_insn_b(size4u_t encoding)
{
    return (encoding >> 16) & 0x00001fff;
}

static unsigned int
decode_insns_tablewalk(insn_t *insn, dectree_table_t *table, size4u_t encoding)
{
    unsigned int i;
    unsigned int a, b;
    opcode_t opc;
    if (table->lookup_function) {
        i = table->lookup_function(table->startbit, table->width, encoding);
    } else {
        i = ((encoding >> table->startbit) & ((1 << table->width) - 1));
    }
    if (table->table[i].type == DECTREE_TABLE_LINK) {
        return decode_insns_tablewalk(insn, table->table[i].table_link,
                                      encoding);
    } else if (table->table[i].type == DECTREE_SUBINSNS) {
        a = get_insn_a(encoding);
        b = get_insn_b(encoding);
        b = decode_subinsn_tablewalk(insn, table->table[i].table_link_b, b);
        a = decode_subinsn_tablewalk(insn + 1, table->table[i].table_link, a);
        if ((a == 0) || (b == 0)) {
            return 0;
        }
        return 2;
    } else if (table->table[i].type == DECTREE_TERMINAL) {
        opc = table->table[i].opcode;
        if ((encoding & decode_itable[opc].mask) != decode_itable[opc].match) {
            if ((encoding & decode_legacy_itable[opc].mask) !=
                decode_legacy_itable[opc].match) {
                return 0;
            }
        }
        decode_op(insn, opc, encoding);
        return 1;
    } else if (table->table[i].type == DECTREE_EXTSPACE) {
        size4u_t active_ext;
        /*
         * For now, HVX will be the only coproc
         * FIXME - Need to check that this thread has acquired a context
         */
        active_ext = 4;
        return decode_insns_tablewalk(insn, ext_trees[active_ext], encoding);
    } else {
        return 0;
    }
}

static unsigned int
decode_insns(insn_t *insn, size4u_t encoding)
{
    dectree_table_t *table;
    if ((encoding & 0x0000c000) != 0) {
        /* Start with PP table */
        table = &dectree_table_DECODE_ROOT_32;
    } else {
        /* start with EE table */
        table = &dectree_table_DECODE_ROOT_EE;
    }
    return decode_insns_tablewalk(insn, table, encoding);
}

static void decode_add_loop_insn(insn_t *insn, int loopnum)
{
    if (loopnum == 10) {
        insn->opcode = J2_endloop01;
        insn->generate = opcode_genptr[J2_endloop01];
    } else if (loopnum == 1) {
        insn->opcode = J2_endloop1;
        insn->generate = opcode_genptr[J2_endloop1];
    } else {
        insn->opcode = J2_endloop0;
        insn->generate = opcode_genptr[J2_endloop0];
    }
}

static inline int decode_parsebits_is_end(size4u_t encoding32)
{
    size4u_t bits = (encoding32 >> 14) & 0x3;
    return ((bits == 0x3) || (bits == 0x0));
}

static inline int decode_parsebits_is_loopend(size4u_t encoding32)
{
    size4u_t bits = (encoding32 >> 14) & 0x3;
    return ((bits == 0x2));
}

static int
decode_set_slot_number(packet_t *pkt)
{
    int slot;
    int i;
    int hit_mem_insn = 0;
    int hit_duplex = 0;
    const char *valid_slot_str;

    for (i = 0, slot = 3; i < pkt->num_insns; i++) {
        if (slot < 0) {
            decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
            warn("[NEWDEFINED] Can't map insns to slots: %s!",
                 opcode_names[pkt->insn[i].opcode]);
            return 1;
        }

        valid_slot_str = get_valid_slot_str(pkt, i);

        while (strchr(valid_slot_str, '0' + slot) == NULL) {
            if (slot <= 0) {
                decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
                warn("[NEWDEFINED] Can't map insns to slots: %s!",
                opcode_names[pkt->insn[i].opcode]);
                return 1;
            }
            slot--;
        }
        pkt->insn[i].slot = slot;
        if (slot) {
            /* I've assigned the slot, now decrement it for the next insn */
            slot--;
        }
    }

    /* Fix the exceptions - mem insns to slot 0,1 */
    for (i = pkt->num_insns - 1; i >= 0; i--) {

        /* First memory instruction always goes to slot 0 */
        if ((GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE) ||
             GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE_PACKET_RULES)) &&
            !hit_mem_insn) {
            hit_mem_insn = 1;
            pkt->insn[i].slot = 0;
            continue;
        }

        /* Next memory instruction always goes to slot 1 */
        if ((GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE) ||
             GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE_PACKET_RULES)) &&
            hit_mem_insn) {
            pkt->insn[i].slot = 1;
        }
    }

    /* Fix the exceptions - duplex always slot 0,1 */
    for (i = pkt->num_insns - 1; i >= 0; i--) {

        /* First subinsn always goes to slot 0 */
        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN) && !hit_duplex) {
            pkt->pkt_has_duplex = 1;
            hit_duplex = 1;
            pkt->insn[i].slot = 0;
            continue;
        }

        /* Next subinsn always goes to slot 1 */
        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN) && hit_duplex) {
            pkt->insn[i].slot = 1;
        }
    }

    /* Fix the exceptions - slot 1 is never empty, always aligns to slot 0 */
    {
        int slot0_found = 0;
        int slot1_found = 0;
        int slot1_iidx = 0;
        for (i = pkt->num_insns - 1; i >= 0; i--) {
            /* Is slot0 used? */
            if (pkt->insn[i].slot == 0) {
                int is_endloop = (pkt->insn[i].opcode == J2_endloop01);
                is_endloop |= (pkt->insn[i].opcode == J2_endloop0);
                is_endloop |= (pkt->insn[i].opcode == J2_endloop1);

                /*
                 * Make sure it's not endloop since, we're overloading
                 * slot0 for endloop
                 */
                if (!is_endloop) {
                    slot0_found = 1;
                }
            }
            /* Is slot1 used? */
            if (pkt->insn[i].slot == 1) {
                slot1_found = 1;
                slot1_iidx = i;
            }
        }
        /* Is slot0 empty and slot1 used? */
        if ((slot0_found == 0) && (slot1_found == 1)) {
            /* Then push it to slot0 */
            pkt->insn[slot1_iidx].slot = 0;
        }
    }
    return 0;
}

/*
 * do_decode_packet
 * Decodes packet with given words
 * Returns negative on error, 0 on insufficient words,
 * and number of words used on success
 */

static int do_decode_packet(int max_words, const size4u_t *words, packet_t *pkt)
{
    int num_insns = 0;
    int words_read = 0;
    int end_of_packet = 0;
    int new_insns = 0;
    int num_mems = 0;
    int errors = 0;
    int i;
    size4u_t encoding32;

    /* Initialize */
    memset(pkt, 0, sizeof(*pkt));
    /* Try to build packet */
    while (!end_of_packet && (words_read < max_words)) {
        encoding32 = words[words_read];
        end_of_packet = decode_parsebits_is_end(encoding32);
        new_insns = decode_insns(&pkt->insn[num_insns], encoding32);
        if (new_insns == 0) {
            warn("bad decode of insn, PC_VA=0x%08x", PC_VA);
            decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
            return -1;
        }
        for (i = 0; i < new_insns; i++) {
            check_twowrite(&pkt->insn[num_insns + i]);
        }
        /*
         * If we saw an extender, mark next word extended so immediate
         * decode works
         */
        if (pkt->insn[num_insns].opcode == A4_ext) {
            pkt->insn[num_insns + 1].extension_valid = 1;
            pkt->pkt_has_payload = 1;
        }
        num_insns += new_insns;
        words_read++;
    }

    pkt->num_insns = num_insns;
    if (!end_of_packet) {
        /* Ran out of words! */
        warn("ran out of words...enc=%08x [%d/%d]",
             encoding32, words_read - 1, max_words);
        decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
        return 0;
    }
    pkt->encod_pkt_size_in_bytes = words_read * 4;
    /* Check packet / aux info */
    for (i = 0; i < num_insns; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, A_MEMCPY)) {
            num_mems += 2;
        } else if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_STORE)) {
            num_mems++;
        }
        if (pkt->insn[i].opcode == A4_ext) {
            pkt->insn[i + 1].extension_valid = 1;
            pkt->pkt_has_payload = 1;
        }
    }
    pkt->pkt_has_extension = 0;
    pkt->pkt_has_initloop = 0;
    pkt->pkt_has_initloop0 = 0;
    pkt->pkt_has_initloop1 = 0;
    for (i = 0; i < num_insns; i++) {
        pkt->pkt_has_extension |=
            GET_ATTRIB(pkt->insn[i].opcode, A_EXTENSION);
        pkt->pkt_has_initloop0 |=
            GET_ATTRIB(pkt->insn[i].opcode, A_HWLOOP0_SETUP);
        pkt->pkt_has_initloop1 |=
            GET_ATTRIB(pkt->insn[i].opcode, A_HWLOOP1_SETUP);
    }
    pkt->pkt_has_initloop |= pkt->pkt_has_initloop0 | pkt->pkt_has_initloop1;

    if (num_mems > 2) {
        decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
        return -2;
    }
    /* Shuffle / split / reorder for execution */
    if ((words_read == 2) && (decode_parsebits_is_loopend(words[0]))) {
        decode_add_loop_insn(&pkt->insn[pkt->num_insns++], 0);
    }
    if (words_read >= 3) {
        /* refactor somehow */
        size4u_t has_loop0, has_loop1;
        has_loop0 = decode_parsebits_is_loopend(words[0]);
        has_loop1 = decode_parsebits_is_loopend(words[1]);
        if (has_loop0 && has_loop1) {
            decode_add_loop_insn(&pkt->insn[pkt->num_insns++], 10);
        } else if (has_loop1) {
            decode_add_loop_insn(&pkt->insn[pkt->num_insns++], 1);
        } else if (has_loop0) {
            decode_add_loop_insn(&pkt->insn[pkt->num_insns++], 0);
        }
    }

    errors += decode_apply_extenders(pkt);
    errors += decode_remove_extenders(pkt);
    errors += decode_set_slot_number(pkt);
    errors += decode_assembler_checks(pkt);
    errors += decode_check_latepred(pkt);
    errors += decode_fill_newvalue_regno(pkt);

    errors += decode_audio_extensions(pkt);

    if (pkt->pkt_has_extension) {
        errors += mmvec_ext_decode_checks(pkt);
    }

    errors += decode_shuffle_for_execution(pkt);
    errors += decode_split_cmpjump(pkt);
    errors += decode_set_insn_attr_fields(pkt);
    if (errors) {
        return -1;
    }

    return words_read;
}
