/*
 * QEMU RISC-V Disassembler for xventana.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xventana.h"

typedef enum {
    /* 0 is reserved for rv_op_illegal. */
    ventana_op_vt_maskc = 1,
    ventana_op_vt_maskcn = 2,
} rv_ventana_op;

const rv_opcode_data ventana_opcode_data[] = {
    { "vt.illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    { "vt.maskc", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "vt.maskcn", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
};

void decode_xventanacondops(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 30:
            switch (((inst >> 22) & 0b1111111000) | ((inst >> 12) & 0b0000000111)) {
            case 6: op = ventana_op_vt_maskc; break;
            case 7: op = ventana_op_vt_maskcn; break;
            }
            break;
        }
        break;
    }

    dec->op = op;
}
