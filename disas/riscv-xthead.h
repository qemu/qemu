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

const rv_opcode_data *decode_xtheadba(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadbb(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadbs(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadcmo(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadcondmov(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadfmemidx(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadfmv(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadmac(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadmemidx(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadmempair(rv_decode *, rv_isa);
const rv_opcode_data *decode_xtheadsync(rv_decode *, rv_isa);

#endif /* DISAS_RISCV_XTHEAD_H */
