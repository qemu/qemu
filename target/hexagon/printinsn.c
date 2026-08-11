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
#include "decode.h"

/*
 * Used when there is some sort of error and we can't figure out the real
 * system register name
 */
static const char *const generic_sreg_names[256] = {
    "S000", "S001", "S002", "S003", "S004", "S005", "S006", "S007",
    "S008", "S009", "S010", "S011", "S012", "S013", "S014", "S015",
    "S016", "S017", "S018", "S019", "S020", "S021", "S022", "S023",
    "S024", "S025", "S026", "S027", "S028", "S029", "S030", "S031",
    "S032", "S033", "S034", "S035", "S036", "S037", "S038", "S039",
    "S040", "S041", "S042", "S043", "S044", "S045", "S046", "S047",
    "S048", "S049", "S050", "S051", "S052", "S053", "S054", "S055",
    "S056", "S057", "S058", "S059", "S060", "S061", "S062", "S063",
    "S064", "S065", "S066", "S067", "S068", "S069", "S070", "S071",
    "S072", "S073", "S074", "S075", "S076", "S077", "S078", "S079",
    "S080", "S081", "S082", "S083", "S084", "S085", "S086", "S087",
    "S088", "S089", "S090", "S091", "S092", "S093", "S094", "S095",
    "S096", "S097", "S098", "S099", "S100", "S101", "S102", "S103",
    "S104", "S105", "S106", "S107", "S108", "S109", "S110", "S111",
    "S112", "S113", "S114", "S115", "S116", "S117", "S118", "S119",
    "S120", "S121", "S122", "S123", "S124", "S125", "S126", "S127",
    "S128", "S129", "S130", "S131", "S132", "S133", "S134", "S135",
    "S136", "S137", "S138", "S139", "S140", "S141", "S142", "S143",
    "S144", "S145", "S146", "S147", "S148", "S149", "S150", "S151",
    "S152", "S153", "S154", "S155", "S156", "S157", "S158", "S159",
    "S160", "S161", "S162", "S163", "S164", "S165", "S166", "S167",
    "S168", "S169", "S170", "S171", "S172", "S173", "S174", "S175",
    "S176", "S177", "S178", "S179", "S180", "S181", "S182", "S183",
    "S184", "S185", "S186", "S187", "S188", "S189", "S190", "S191",
    "S192", "S193", "S194", "S195", "S196", "S197", "S198", "S199",
    "S200", "S201", "S202", "S203", "S204", "S205", "S206", "S207",
    "S208", "S209", "S210", "S211", "S212", "S213", "S214", "S215",
    "S216", "S217", "S218", "S219", "S220", "S221", "S222", "S223",
    "S224", "S225", "S226", "S227", "S228", "S229", "S230", "S231",
    "S232", "S233", "S234", "S235", "S236", "S237", "S238", "S239",
    "S240", "S241", "S242", "S243", "S244", "S245", "S246", "S247",
    "S248", "S249", "S250", "S251", "S252", "S253", "S254", "S255",
};

static const char *sreg2str(uint8_t reg)
{
#ifndef CONFIG_USER_ONLY
    if (reg < NUM_SREGS) {
        return hexagon_sregnames[reg];
    } else {
        return generic_sreg_names[reg];
    }
#else
    return generic_sreg_names[reg];
#endif
}

/*
 * Used when there is some sort of error and we can't figure out the real
 * control register name
 */
static const char *const generic_creg_names[256] = {
    "C000", "C001", "C002", "C003", "C004", "C005", "C006", "C007",
    "C008", "C009", "C010", "C011", "C012", "C013", "C014", "C015",
    "C016", "C017", "C018", "C019", "C020", "C021", "C022", "C023",
    "C024", "C025", "C026", "C027", "C028", "C029", "C030", "C031",
    "C032", "C033", "C034", "C035", "C036", "C037", "C038", "C039",
    "C040", "C041", "C042", "C043", "C044", "C045", "C046", "C047",
    "C048", "C049", "C050", "C051", "C052", "C053", "C054", "C055",
    "C056", "C057", "C058", "C059", "C060", "C061", "C062", "C063",
    "C064", "C065", "C066", "C067", "C068", "C069", "C070", "C071",
    "C072", "C073", "C074", "C075", "C076", "C077", "C078", "C079",
    "C080", "C081", "C082", "C083", "C084", "C085", "C086", "C087",
    "C088", "C089", "C090", "C091", "C092", "C093", "C094", "C095",
    "C096", "C097", "C098", "C099", "C100", "C101", "C102", "C103",
    "C104", "C105", "C106", "C107", "C108", "C109", "C110", "C111",
    "C112", "C113", "C114", "C115", "C116", "C117", "C118", "C119",
    "C120", "C121", "C122", "C123", "C124", "C125", "C126", "C127",
    "C128", "C129", "C130", "C131", "C132", "C133", "C134", "C135",
    "C136", "C137", "C138", "C139", "C140", "C141", "C142", "C143",
    "C144", "C145", "C146", "C147", "C148", "C149", "C150", "C151",
    "C152", "C153", "C154", "C155", "C156", "C157", "C158", "C159",
    "C160", "C161", "C162", "C163", "C164", "C165", "C166", "C167",
    "C168", "C169", "C170", "C171", "C172", "C173", "C174", "C175",
    "C176", "C177", "C178", "C179", "C180", "C181", "C182", "C183",
    "C184", "C185", "C186", "C187", "C188", "C189", "C190", "C191",
    "C192", "C193", "C194", "C195", "C196", "C197", "C198", "C199",
    "C200", "C201", "C202", "C203", "C204", "C205", "C206", "C207",
    "C208", "C209", "C210", "C211", "C212", "C213", "C214", "C215",
    "C216", "C217", "C218", "C219", "C220", "C221", "C222", "C223",
    "C224", "C225", "C226", "C227", "C228", "C229", "C230", "C231",
    "C232", "C233", "C234", "C235", "C236", "C237", "C238", "C239",
    "C240", "C241", "C242", "C243", "C244", "C245", "C246", "C247",
    "C248", "C249", "C250", "C251", "C252", "C253", "C254", "C255",
};

static const char *creg2str(uint8_t reg)
{
    uint8_t gpr = reg + HEX_REG_SA0;
    if (gpr < TOTAL_PER_THREAD_REGS) {
        return hexagon_regnames[gpr];
    } else {
        return generic_creg_names[reg];
    }
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
                         target_ulong pc, const HexagonCPUDef *hex_def)
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
        if (opcode_supported(pkt->insn[i].opcode, hex_def)) {
            snprintinsn(buf, &(pkt->insn[i]));
        } else {
            g_string_append(buf, "<invalid>");
        }

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
