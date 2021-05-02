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

#ifndef HEXAGON_INTERNAL_H
#define HEXAGON_INTERNAL_H

/*
 * Change HEX_DEBUG to 1 to turn on debugging output
 */
#define HEX_DEBUG 0
#define HEX_DEBUG_LOG(...) \
    do { \
        if (HEX_DEBUG) { \
            qemu_log(__VA_ARGS__); \
        } \
    } while (0)

int hexagon_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int hexagon_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void hexagon_debug(CPUHexagonState *env);

extern const char * const hexagon_regnames[TOTAL_PER_THREAD_REGS];

#endif
