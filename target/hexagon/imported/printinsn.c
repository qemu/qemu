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

#include <stdio.h>
#include <string.h>
#include "qemu/osdep.h"
#include "opcodes.h"
#include "printinsn.h"
#include "insn.h"
#include "macros.h"

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
    return sreg2str(reg + NUM_GEN_REGS);
}

static void snprintinsn(char *buf, int n, insn_t * insn)
{
    switch (insn->opcode) {
#define DEF_VECX_PRINTINFO(TAG, FMT, ...) DEF_PRINTINFO(TAG, FMT, __VA_ARGS__)
#define DEF_PRINTINFO(TAG, FMT, ...) \
    case TAG: \
        snprintf(buf, n, FMT, __VA_ARGS__);\
        break;
#include "printinsn_generated.h"
#undef DEF_VECX_PRINTINFO
#undef DEF_PRINTINFO
    }
}

void snprint_a_pkt(char *buf, int n, packet_t * pkt)
{
    char tmpbuf[128];
    buf[0] = '\0';
    int i, slot, opcode;

    if (pkt == NULL) {
        snprintf(buf, n, "<printpkt: NULL ptr>");
        return;
    }

    if (pkt->num_insns > 1) {
        strncat(buf, "\n{\n", n);
    }
    for (i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            continue;
        }
        snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
        strncat(buf, "\t", n);
        strncat(buf, tmpbuf, n);
        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
            strncat(buf, " //subinsn", n);
        }
        if (pkt->insn[i].extension_valid) {
            strncat(buf, " //constant extended", n);
        }
        slot = pkt->insn[i].slot;
        opcode = pkt->insn[i].opcode;
        snprintf(tmpbuf, 127, " //slot=%d:tag=%s", slot, opcode_names[opcode]);
        strncat(buf, tmpbuf, n);

        strncat(buf, "\n", n);
    }
    if (pkt->num_insns > 1) {
        strncat(buf, "}\n", n);
    }
}

