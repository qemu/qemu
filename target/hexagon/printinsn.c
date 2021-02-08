/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "attribs.h"
#include "printinsn.h"
#include "insn.h"
#include "reg_fields.h"
#include "internal.h"

static const char *sreg2str(unsigned int reg)
{
    if (reg < TOTAL_PER_THREAD_REGS) {
        return hexagon_regnames[reg];
    } else {
        return "???";
    }
}

static const char *creg2str(unsigned int reg)
{
    return sreg2str(reg + HEX_REG_SA0);
}

static void snprintinsn(GString *buf, Insn *insn)
{
    switch (insn->opcode) {
#define DEF_VECX_PRINTINFO(TAG, FMT, ...) DEF_PRINTINFO(TAG, FMT, __VA_ARGS__)
#define DEF_PRINTINFO(TAG, FMT, ...) \
    case TAG: \
        g_string_append_printf(buf, FMT, __VA_ARGS__); \
        break;
#include "printinsn_generated.h.inc"
#undef DEF_VECX_PRINTINFO
#undef DEF_PRINTINFO
    }
}

void snprint_a_pkt_disas(GString *buf, Packet *pkt, uint32_t *words,
                         target_ulong pc)
{
    bool has_endloop0 = false;
    bool has_endloop1 = false;
    bool has_endloop01 = false;

    for (int i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            continue;
        }

        /* We'll print the endloop's at the end of the packet */
        if (pkt->insn[i].opcode == J2_endloop0) {
            has_endloop0 = true;
            continue;
        }
        if (pkt->insn[i].opcode == J2_endloop1) {
            has_endloop1 = true;
            continue;
        }
        if (pkt->insn[i].opcode == J2_endloop01) {
            has_endloop01 = true;
            continue;
        }

        g_string_append_printf(buf, "0x" TARGET_FMT_lx "\t", words[i]);

        if (i == 0) {
            g_string_append(buf, "{");
        }

        g_string_append(buf, "\t");
        snprintinsn(buf, &(pkt->insn[i]));

        if (i < pkt->num_insns - 1) {
            /*
             * Subinstructions are two instructions encoded
             * in the same word. Print them on the same line.
             */
            if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
                g_string_append(buf, "; ");
                snprintinsn(buf, &(pkt->insn[i + 1]));
                i++;
            } else if (pkt->insn[i + 1].opcode != J2_endloop0 &&
                       pkt->insn[i + 1].opcode != J2_endloop1 &&
                       pkt->insn[i + 1].opcode != J2_endloop01) {
                pc += 4;
                g_string_append_printf(buf, "\n0x" TARGET_FMT_lx ":  ", pc);
            }
        }
    }
    g_string_append(buf, " }");
    if (has_endloop0) {
        g_string_append(buf, "  :endloop0");
    }
    if (has_endloop1) {
        g_string_append(buf, "  :endloop1");
    }
    if (has_endloop01) {
        g_string_append(buf, "  :endloop01");
    }
}

void snprint_a_pkt_debug(GString *buf, Packet *pkt)
{
    int slot, opcode;

    if (pkt->num_insns > 1) {
        g_string_append(buf, "\n{\n");
    }

    for (int i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            continue;
        }
        g_string_append(buf, "\t");
        snprintinsn(buf, &(pkt->insn[i]));

        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
            g_string_append(buf, " //subinsn");
        }
        if (pkt->insn[i].extension_valid) {
            g_string_append(buf, " //constant extended");
        }
        slot = pkt->insn[i].slot;
        opcode = pkt->insn[i].opcode;
        g_string_append_printf(buf, " //slot=%d:tag=%s\n",
                               slot, opcode_names[opcode]);
    }
    if (pkt->num_insns > 1) {
        g_string_append(buf, "}\n");
    }
}
