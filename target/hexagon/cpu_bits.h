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

#ifndef HEXAGON_CPU_BITS_H
#define HEXAGON_CPU_BITS_H

#include "qemu/bitops.h"

#define PCALIGN 4
#define PCALIGN_MASK (PCALIGN - 1)

enum hex_event {
    HEX_EVENT_NONE = -1,
    HEX_EVENT_RESET = 0x0,
    HEX_EVENT_IMPRECISE = 0x1,
    HEX_EVENT_PRECISE = 0x2,
    HEX_EVENT_TLB_MISS_X = 0x4,
    HEX_EVENT_TLB_MISS_RW = 0x6,
    HEX_EVENT_TRAP0 = 0x8,
    HEX_EVENT_TRAP1 = 0x9,
    HEX_EVENT_FPTRAP = 0xb,
    HEX_EVENT_DEBUG = 0xc,
    HEX_EVENT_INT0 = 0x10,
    HEX_EVENT_INT1 = 0x11,
    HEX_EVENT_INT2 = 0x12,
    HEX_EVENT_INT3 = 0x13,
    HEX_EVENT_INT4 = 0x14,
    HEX_EVENT_INT5 = 0x15,
    HEX_EVENT_INT6 = 0x16,
    HEX_EVENT_INT7 = 0x17,
    HEX_EVENT_INT8 = 0x18,
    HEX_EVENT_INT9 = 0x19,
    HEX_EVENT_INTA = 0x1a,
    HEX_EVENT_INTB = 0x1b,
    HEX_EVENT_INTC = 0x1c,
    HEX_EVENT_INTD = 0x1d,
    HEX_EVENT_INTE = 0x1e,
    HEX_EVENT_INTF = 0x1f,
};

enum hex_cause {
    HEX_CAUSE_NONE = -1,
    HEX_CAUSE_RESET = 0x000,
    HEX_CAUSE_BIU_PRECISE = 0x001,
    HEX_CAUSE_UNSUPORTED_HVX_64B = 0x002, /* QEMU-specific */
    HEX_CAUSE_DOUBLE_EXCEPT = 0x003,
    HEX_CAUSE_TRAP0 = 0x008,
    HEX_CAUSE_TRAP1 = 0x009,
    HEX_CAUSE_FETCH_NO_XPAGE = 0x011,
    HEX_CAUSE_FETCH_NO_UPAGE = 0x012,
    HEX_CAUSE_INVALID_PACKET = 0x015,
    HEX_CAUSE_INVALID_OPCODE = 0x015,
    HEX_CAUSE_NO_COPROC_ENABLE = 0x016,
    HEX_CAUSE_NO_COPROC2_ENABLE = 0x018,
    HEX_CAUSE_PRIV_USER_NO_GINSN = 0x01a,
    HEX_CAUSE_PRIV_USER_NO_SINSN = 0x01b,
    HEX_CAUSE_REG_WRITE_CONFLICT = 0x01d,
    HEX_CAUSE_PC_NOT_ALIGNED = 0x01e,
    HEX_CAUSE_MISALIGNED_LOAD = 0x020,
    HEX_CAUSE_MISALIGNED_STORE = 0x021,
    HEX_CAUSE_PRIV_NO_READ = 0x022,
    HEX_CAUSE_PRIV_NO_WRITE = 0x023,
    HEX_CAUSE_PRIV_NO_UREAD = 0x024,
    HEX_CAUSE_PRIV_NO_UWRITE = 0x025,
    HEX_CAUSE_COPROC_LDST = 0x026,
    HEX_CAUSE_STACK_LIMIT = 0x027,
    HEX_CAUSE_VWCTRL_WINDOW_MISS = 0x029,
    HEX_CAUSE_IMPRECISE_NMI = 0x043,
    HEX_CAUSE_IMPRECISE_MULTI_TLB_MATCH = 0x044,
    HEX_CAUSE_TLBMISSX_CAUSE_NORMAL = 0x060,
    HEX_CAUSE_TLBMISSX_CAUSE_NEXTPAGE = 0x061,
    HEX_CAUSE_TLBMISSRW_CAUSE_READ = 0x070,
    HEX_CAUSE_TLBMISSRW_CAUSE_WRITE = 0x071,
    HEX_CAUSE_DEBUG_SINGLESTEP = 0x80,
    HEX_CAUSE_FPTRAP_CAUSE_BADFLOAT = 0x0bf,
    HEX_CAUSE_INT0 = 0x0c0,
    HEX_CAUSE_INT1 = 0x0c1,
    HEX_CAUSE_INT2 = 0x0c2,
    HEX_CAUSE_INT3 = 0x0c3,
    HEX_CAUSE_INT4 = 0x0c4,
    HEX_CAUSE_INT5 = 0x0c5,
    HEX_CAUSE_INT6 = 0x0c6,
    HEX_CAUSE_INT7 = 0x0c7,
    HEX_CAUSE_VIC0 = 0x0c2,
    HEX_CAUSE_VIC1 = 0x0c3,
    HEX_CAUSE_VIC2 = 0x0c4,
    HEX_CAUSE_VIC3 = 0x0c5,
};

enum data_cache_state {
    HEX_DC_STATE_INVALID   = 0x0,
    HEX_DC_STATE_VALID     = 0x1,
    HEX_DC_STATE_RESERVED  = 0x2,
    HEX_DC_STATE_UNUSED_WT = 0x3,
};

#define PACKET_WORDS_MAX         4

static inline uint32_t parse_bits(uint32_t encoding)
{
    /* The parse bits are [15:14] */
    return extract32(encoding, 14, 2);
}

static inline uint32_t iclass_bits(uint32_t encoding)
{
    /* The instruction class is encoded in bits [31:28] */
    uint32_t iclass = extract32(encoding, 28, 4);
    /* If parse bits are zero, this is a duplex */
    if (parse_bits(encoding) == 0) {
        iclass += 16;
    }
    return iclass;
}

static inline bool is_packet_end(uint32_t endocing)
{
    uint32_t bits = parse_bits(endocing);
    return ((bits == 0x3) || (bits == 0x0));
}

int disassemble_hexagon(uint32_t *words, int nwords, bfd_vma pc, GString *buf);

#endif
