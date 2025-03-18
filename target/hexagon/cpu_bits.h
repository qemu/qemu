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
    HEX_EVENT_NONE           = -1,
    HEX_EVENT_TRAP0          =  0x008,
};

enum hex_cause {
    HEX_CAUSE_NONE = -1,
    HEX_CAUSE_TRAP0 = 0x172,
    HEX_CAUSE_FETCH_NO_UPAGE =  0x012,
    HEX_CAUSE_INVALID_PACKET =  0x015,
    HEX_CAUSE_INVALID_OPCODE =  0x015,
    HEX_CAUSE_PC_NOT_ALIGNED =  0x01e,
    HEX_CAUSE_PRIV_NO_UREAD  =  0x024,
    HEX_CAUSE_PRIV_NO_UWRITE =  0x025,
    HEX_CAUSE_PRIV_USER_NO_GINSN = 0x01a,
    HEX_CAUSE_PRIV_USER_NO_SINSN = 0x01b,
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
