/*
 * Copyright (c) 2017, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "hw/xtensa/xtensa-isa.h"

int print_insn_xtensa(bfd_vma memaddr, struct disassemble_info *info)
{
    xtensa_isa isa = info->private_data;
    xtensa_insnbuf insnbuf = xtensa_insnbuf_alloc(isa);
    xtensa_insnbuf slotbuf = xtensa_insnbuf_alloc(isa);
    bfd_byte *buffer = g_malloc(1);
    int status = info->read_memory_func(memaddr, buffer, 1, info);
    xtensa_format fmt;
    int slot, slots;
    unsigned len;

    if (status) {
        info->memory_error_func(status, memaddr, info);
        len = -1;
        goto out;
    }
    len = xtensa_isa_length_from_chars(isa, buffer);
    if (len == XTENSA_UNDEFINED) {
        info->fprintf_func(info->stream, ".byte 0x%02x", buffer[0]);
        len = 1;
        goto out;
    }
    buffer = g_realloc(buffer, len);
    status = info->read_memory_func(memaddr + 1, buffer + 1, len - 1, info);
    if (status) {
        info->fprintf_func(info->stream, ".byte 0x%02x", buffer[0]);
        info->memory_error_func(status, memaddr + 1, info);
        len = 1;
        goto out;
    }

    xtensa_insnbuf_from_chars(isa, insnbuf, buffer, len);
    fmt = xtensa_format_decode(isa, insnbuf);
    if (fmt == XTENSA_UNDEFINED) {
        unsigned i;

        for (i = 0; i < len; ++i) {
            info->fprintf_func(info->stream, "%s 0x%02x",
                               i ? ", " : ".byte ", buffer[i]);
        }
        goto out;
    }
    slots = xtensa_format_num_slots(isa, fmt);

    if (slots > 1) {
        info->fprintf_func(info->stream, "{ ");
    }

    for (slot = 0; slot < slots; ++slot) {
        xtensa_opcode opc;
        int opnd, vopnd, opnds;

        if (slot) {
            info->fprintf_func(info->stream, "; ");
        }
        xtensa_format_get_slot(isa, fmt, slot, insnbuf, slotbuf);
        opc = xtensa_opcode_decode(isa, fmt, slot, slotbuf);
        if (opc == XTENSA_UNDEFINED) {
            info->fprintf_func(info->stream, "???");
            continue;
        }
        opnds = xtensa_opcode_num_operands(isa, opc);

        info->fprintf_func(info->stream, "%s", xtensa_opcode_name(isa, opc));

        for (opnd = vopnd = 0; opnd < opnds; ++opnd) {
            if (xtensa_operand_is_visible(isa, opc, opnd)) {
                uint32_t v = 0xbadc0de;
                int rc;

                info->fprintf_func(info->stream, vopnd ? ", " : "\t");
                xtensa_operand_get_field(isa, opc, opnd, fmt, slot,
                                         slotbuf, &v);
                rc = xtensa_operand_decode(isa, opc, opnd, &v);
                if (rc == XTENSA_UNDEFINED) {
                    info->fprintf_func(info->stream, "???");
                } else if (xtensa_operand_is_register(isa, opc, opnd)) {
                    xtensa_regfile rf = xtensa_operand_regfile(isa, opc, opnd);

                    info->fprintf_func(info->stream, "%s%d",
                                       xtensa_regfile_shortname(isa, rf), v);
                } else if (xtensa_operand_is_PCrelative(isa, opc, opnd)) {
                    xtensa_operand_undo_reloc(isa, opc, opnd, &v, memaddr);
                    info->fprintf_func(info->stream, "0x%x", v);
                } else {
                    info->fprintf_func(info->stream, "%d", v);
                }
                ++vopnd;
            }
        }
    }
    if (slots > 1) {
        info->fprintf_func(info->stream, " }");
    }

out:
    g_free(buffer);
    xtensa_insnbuf_free(isa, insnbuf);
    xtensa_insnbuf_free(isa, slotbuf);

    return len;
}
