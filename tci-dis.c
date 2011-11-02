/*
 * Tiny Code Interpreter for QEMU - disassembler
 *
 * Copyright (c) 2011 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dis-asm.h"
#include "tcg/tcg.h"

/* Disassemble TCI bytecode. */
int print_insn_tci(bfd_vma addr, disassemble_info *info)
{
    int length;
    uint8_t byte;
    int status;
    TCGOpcode op;

    status = info->read_memory_func(addr, &byte, 1, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }
    op = byte;

    addr++;
    status = info->read_memory_func(addr, &byte, 1, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }
    length = byte;

    if (op >= tcg_op_defs_max) {
        info->fprintf_func(info->stream, "illegal opcode %d", op);
    } else {
        const TCGOpDef *def = &tcg_op_defs[op];
        int nb_oargs = def->nb_oargs;
        int nb_iargs = def->nb_iargs;
        int nb_cargs = def->nb_cargs;
        /* TODO: Improve disassembler output. */
        info->fprintf_func(info->stream, "%s\to=%d i=%d c=%d",
                           def->name, nb_oargs, nb_iargs, nb_cargs);
    }

    return length;
}
