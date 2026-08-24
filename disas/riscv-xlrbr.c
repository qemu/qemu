/*
 * QEMU RISC-V Disassembler for xlrbr matching the unratified Zbr CRC32
 * bitmanip extension v0.93.
 *
 * Copyright (c) 2023 Rivos Inc
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xlrbr.h"

#define OP(N, ...) static const rv_opcode_data op_##N = { __VA_ARGS__ };
#include "riscv-xlrbr-op.c.inc"
#undef OP

const rv_opcode_data *decode_xlrbr(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch ((inst >> 0) & 0b1111111) {
    case 0b0010011:
        switch ((inst >> 12) & 0b111) {
        case 0b001:
            switch ((inst >> 20 & 0b111111111111)) {
            case 0b011000010000:
                return &op_crc32_b;
            case 0b011000010001:
                return &op_crc32_h;
            case 0b011000010010:
                return &op_crc32_w;
            case 0b011000010011:
                return &op_crc32_d;
            case 0b011000011000:
                return &op_crc32c_b;
            case 0b011000011001:
                return &op_crc32c_h;
            case 0b011000011010:
                return &op_crc32c_w;
            case 0b011000011011:
                return &op_crc32c_d;
            }
            break;
        }
        break;
    }

    return NULL;
}
