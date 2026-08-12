/*
 * QEMU RISC-V Disassembler for xlrbr matching the unratified Zbr CRC32
 * bitmanip extension v0.93.
 *
 * Copyright (c) 2023 Rivos Inc
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_XLRBR_H
#define DISAS_RISCV_XLRBR_H

#include "disas/riscv.h"

const rv_opcode_data *decode_xlrbr(rv_decode *, rv_isa);

#endif /* DISAS_RISCV_XLRBR_H */
