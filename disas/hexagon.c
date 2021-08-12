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

/*
 * QEMU Hexagon Disassembler
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "target/hexagon/cpu_bits.h"

/*
 * We will disassemble a packet with up to 4 instructions, so we need
 * a hefty size buffer.
 */
#define PACKET_BUFFER_LEN                   1028

int print_insn_hexagon(bfd_vma memaddr, struct disassemble_info *info)
{
    uint32_t words[PACKET_WORDS_MAX];
    bool found_end = false;
    GString *buf;
    int i, len;

    for (i = 0; i < PACKET_WORDS_MAX && !found_end; i++) {
        int status = (*info->read_memory_func)(memaddr + i * sizeof(uint32_t),
                                               (bfd_byte *)&words[i],
                                               sizeof(uint32_t), info);
        if (status) {
            if (i > 0) {
                break;
            }
            (*info->memory_error_func)(status, memaddr, info);
            return status;
        }
        if (is_packet_end(words[i])) {
            found_end = true;
        }
    }

    if (!found_end) {
        (*info->fprintf_func)(info->stream, "<invalid>");
        return PACKET_WORDS_MAX * sizeof(uint32_t);
    }

    buf = g_string_sized_new(PACKET_BUFFER_LEN);
    len = disassemble_hexagon(words, i, memaddr, buf);
    (*info->fprintf_func)(info->stream, "%s", buf->str);
    g_string_free(buf, true);

    return len;
}
