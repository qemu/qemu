/*
 * QEMU RISC-V Disassembler for xventana.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xventana.h"

#define OP(N, ...) static const rv_opcode_data op_##N = { __VA_ARGS__ };
#include "riscv-xventana-op.c.inc"
#undef OP

const rv_opcode_data *decode_xventanacondops(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 30:
            switch (((inst >> 22) & 0b1111111000) | ((inst >> 12) & 0b0000000111)) {
            case 6: return &op_vt_maskc;
            case 7: return &op_vt_maskcn;
            }
            break;
        }
        break;
    }

    return NULL;
}
