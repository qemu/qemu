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

#ifndef HEXAGON_OP_HELPER_H
#define HEXAGON_OP_HELPER_H

/* Misc functions */
uint8_t mem_load1(CPUHexagonState *env, bool pkt_has_store_s1,
                  uint32_t slot, target_ulong vaddr);
uint16_t mem_load2(CPUHexagonState *env, bool pkt_has_store_s1,
                   uint32_t slot, target_ulong vaddr);
uint32_t mem_load4(CPUHexagonState *env, bool pkt_has_store_s1,
                   uint32_t slot, target_ulong vaddr);
uint64_t mem_load8(CPUHexagonState *env, bool pkt_has_store_s1,
                   uint32_t slot, target_ulong vaddr);

void log_store64(CPUHexagonState *env, target_ulong addr,
                 int64_t val, int width, int slot);
void log_store32(CPUHexagonState *env, target_ulong addr,
                 target_ulong val, int width, int slot);

#endif
