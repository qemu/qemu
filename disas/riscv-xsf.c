/*
 * QEMU RISC-V Disassembler for xsf (SiFive vendor extensions).
 *
 * Copyright (c) 2023 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xsf.h"

#define OP(N, ...) static const rv_opcode_data op_##N = { __VA_ARGS__ };
#include "riscv-xsf-op.c.inc"
#undef OP

const rv_opcode_data *decode_xsf(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch ((inst >> 0) & 0b1111111) {
    case 0b1011011:
        switch ((inst >> 12) & 0b111) {
        case 0b010:
            switch ((inst >> 26) & 0b111111) {
            case 0b101100:
                return &op_sf_vqmaccu_2x8x2;
            case 0b101101:
                return &op_sf_vqmacc_2x8x2;
            case 0b101110:
                return &op_sf_vqmaccus_2x8x2;
            case 0b101111:
                return &op_sf_vqmaccsu_2x8x2;
            case 0b111100:
                return &op_sf_vqmaccu_4x8x4;
            case 0b111101:
                return &op_sf_vqmacc_4x8x4;
            case 0b111110:
                return &op_sf_vqmaccus_4x8x4;
            case 0b111111:
                return &op_sf_vqmaccsu_4x8x4;
            }
            break;
        }
        break;
    }

    return NULL;
}
