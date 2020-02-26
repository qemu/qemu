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

#ifndef HEXAGON_DECODE_H
#define HEXAGON_DECODE_H

#include "cpu.h"
#include "opcodes.h"
#include "hex_arch_types.h"
#include "insn.h"

extern void decode_init(void);

static inline int is_packet_end(uint32_t word)
{
    uint32_t bits = (word >> 14) & 0x3;
    return ((bits == 0x3) || (bits == 0x0));
}

extern void decode_send_insn_to(packet_t *packet, int start, int newloc);

extern packet_t *decode_this(int max_words, size4u_t *words,
                             packet_t *decode_pkt);

#endif
