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

#ifndef HEXAGON_DECODE_H
#define HEXAGON_DECODE_H

#include "cpu.h"
#include "opcodes.h"
#include "insn.h"

void decode_init(void);

void decode_send_insn_to(Packet *packet, int start, int newloc);

int decode_packet(int max_words, const uint32_t *words, Packet *pkt,
                  bool disas_only);

#endif
