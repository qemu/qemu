/*
 * QEMU disassembler -- RISC-V specific header (xthead*).
 *
 * Copyright (c) 2023 VRULL GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_XTHEAD_H
#define DISAS_RISCV_XTHEAD_H

#include "disas/riscv.h"

extern const rv_opcode_data xthead_opcode_data[];

void decode_xtheadba(rv_decode *, rv_isa);
void decode_xtheadbb(rv_decode *, rv_isa);
void decode_xtheadbs(rv_decode *, rv_isa);
void decode_xtheadcmo(rv_decode *, rv_isa);
void decode_xtheadcondmov(rv_decode *, rv_isa);
void decode_xtheadfmemidx(rv_decode *, rv_isa);
void decode_xtheadfmv(rv_decode *, rv_isa);
void decode_xtheadmac(rv_decode *, rv_isa);
void decode_xtheadmemidx(rv_decode *, rv_isa);
void decode_xtheadmempair(rv_decode *, rv_isa);
void decode_xtheadsync(rv_decode *, rv_isa);

#endif /* DISAS_RISCV_XTHEAD_H */
