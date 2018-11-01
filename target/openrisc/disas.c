/*
 * OpenRISC disassembler
 *
 * Copyright (c) 2018 Richard Henderson <rth@twiddle.net>
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "disas/bfd.h"
#include "qemu/bitops.h"
#include "cpu.h"

typedef disassemble_info DisasContext;

/* Include the auto-generated decoder.  */
#include "decode.inc.c"

#define output(mnemonic, format, ...) \
    (info->fprintf_func(info->stream, "%-9s " format, \
                        mnemonic, ##__VA_ARGS__))

int print_insn_or1k(bfd_vma addr, disassemble_info *info)
{
    bfd_byte buffer[4];
    uint32_t insn;
    int status;

    status = info->read_memory_func(addr, buffer, 4, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }
    insn = bfd_getb32(buffer);

    if (!decode(info, insn)) {
        output(".long", "%#08x", insn);
    }
    return 4;
}

#define INSN(opcode, format, ...)                                       \
static bool trans_l_##opcode(disassemble_info *info, arg_l_##opcode *a) \
{                                                                       \
    output("l." #opcode, format, ##__VA_ARGS__);                        \
    return true;                                                        \
}

INSN(add,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(addc,   "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(sub,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(and,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(or,     "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(xor,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(sll,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(srl,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(sra,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(ror,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(exths,  "r%d, r%d", a->d, a->a)
INSN(extbs,  "r%d, r%d", a->d, a->a)
INSN(exthz,  "r%d, r%d", a->d, a->a)
INSN(extbz,  "r%d, r%d", a->d, a->a)
INSN(cmov,   "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(ff1,    "r%d, r%d", a->d, a->a)
INSN(fl1,    "r%d, r%d", a->d, a->a)
INSN(mul,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(mulu,   "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(div,    "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(divu,   "r%d, r%d, r%d", a->d, a->a, a->b)
INSN(muld,   "r%d, r%d", a->a, a->b)
INSN(muldu,  "r%d, r%d", a->a, a->b)
INSN(j,      "%d", a->n)
INSN(jal,    "%d", a->n)
INSN(bf,     "%d", a->n)
INSN(bnf,    "%d", a->n)
INSN(jr,     "r%d", a->b)
INSN(jalr,   "r%d", a->b)
INSN(lwa,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(lwz,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(lws,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(lbz,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(lbs,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(lhz,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(lhs,    "r%d, %d(r%d)", a->d, a->i, a->a)
INSN(swa,    "%d(r%d), r%d", a->i, a->a, a->b)
INSN(sw,     "%d(r%d), r%d", a->i, a->a, a->b)
INSN(sb,     "%d(r%d), r%d", a->i, a->a, a->b)
INSN(sh,     "%d(r%d), r%d", a->i, a->a, a->b)
INSN(nop,    "")
INSN(addi,   "r%d, r%d, %d", a->d, a->a, a->i)
INSN(addic,  "r%d, r%d, %d", a->d, a->a, a->i)
INSN(muli,   "r%d, r%d, %d", a->d, a->a, a->i)
INSN(maci,   "r%d, %d", a->a, a->i)
INSN(andi,   "r%d, r%d, %d", a->d, a->a, a->k)
INSN(ori,    "r%d, r%d, %d", a->d, a->a, a->k)
INSN(xori,   "r%d, r%d, %d", a->d, a->a, a->i)
INSN(mfspr,  "r%d, r%d, %d", a->d, a->a, a->k)
INSN(mtspr,  "r%d, r%d, %d", a->a, a->b, a->k)
INSN(mac,    "r%d, r%d", a->a, a->b)
INSN(msb,    "r%d, r%d", a->a, a->b)
INSN(macu,   "r%d, r%d", a->a, a->b)
INSN(msbu,   "r%d, r%d", a->a, a->b)
INSN(slli,   "r%d, r%d, %d", a->d, a->a, a->l)
INSN(srli,   "r%d, r%d, %d", a->d, a->a, a->l)
INSN(srai,   "r%d, r%d, %d", a->d, a->a, a->l)
INSN(rori,   "r%d, r%d, %d", a->d, a->a, a->l)
INSN(movhi,  "r%d, %d", a->d, a->k)
INSN(macrc,  "r%d", a->d)
INSN(sfeq,   "r%d, r%d", a->a, a->b)
INSN(sfne,   "r%d, r%d", a->a, a->b)
INSN(sfgtu,  "r%d, r%d", a->a, a->b)
INSN(sfgeu,  "r%d, r%d", a->a, a->b)
INSN(sfltu,  "r%d, r%d", a->a, a->b)
INSN(sfleu,  "r%d, r%d", a->a, a->b)
INSN(sfgts,  "r%d, r%d", a->a, a->b)
INSN(sfges,  "r%d, r%d", a->a, a->b)
INSN(sflts,  "r%d, r%d", a->a, a->b)
INSN(sfles,  "r%d, r%d", a->a, a->b)
INSN(sfeqi,  "r%d, %d", a->a, a->i)
INSN(sfnei,  "r%d, %d", a->a, a->i)
INSN(sfgtui, "r%d, %d", a->a, a->i)
INSN(sfgeui, "r%d, %d", a->a, a->i)
INSN(sfltui, "r%d, %d", a->a, a->i)
INSN(sfleui, "r%d, %d", a->a, a->i)
INSN(sfgtsi, "r%d, %d", a->a, a->i)
INSN(sfgesi, "r%d, %d", a->a, a->i)
INSN(sfltsi, "r%d, %d", a->a, a->i)
INSN(sflesi, "r%d, %d", a->a, a->i)
INSN(sys,    "%d", a->k)
INSN(trap,   "%d", a->k)
INSN(msync,  "")
INSN(psync,  "")
INSN(csync,  "")
INSN(rfe,    "")

#define FP_INSN(opcode, suffix, format, ...)                            \
static bool trans_lf_##opcode##_##suffix(disassemble_info *info,        \
                                         arg_lf_##opcode##_##suffix *a) \
{                                                                       \
    output("lf." #opcode "." #suffix, format, ##__VA_ARGS__);           \
    return true;                                                        \
}

FP_INSN(add, s,  "r%d, r%d, r%d", a->d, a->a, a->b)
FP_INSN(sub, s,  "r%d, r%d, r%d", a->d, a->a, a->b)
FP_INSN(mul, s,  "r%d, r%d, r%d", a->d, a->a, a->b)
FP_INSN(div, s,  "r%d, r%d, r%d", a->d, a->a, a->b)
FP_INSN(rem, s,  "r%d, r%d, r%d", a->d, a->a, a->b)
FP_INSN(itof, s, "r%d, r%d", a->d, a->a)
FP_INSN(ftoi, s, "r%d, r%d", a->d, a->a)
FP_INSN(madd, s, "r%d, r%d, r%d", a->d, a->a, a->b)
FP_INSN(sfeq, s, "r%d, r%d", a->a, a->b)
FP_INSN(sfne, s, "r%d, r%d", a->a, a->b)
FP_INSN(sfgt, s, "r%d, r%d", a->a, a->b)
FP_INSN(sfge, s, "r%d, r%d", a->a, a->b)
FP_INSN(sflt, s, "r%d, r%d", a->a, a->b)
FP_INSN(sfle, s, "r%d, r%d", a->a, a->b)
