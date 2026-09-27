/*
 * QEMU disassembler -- RISC-V specific header (xsf*).
 *
 * Copyright (c) 2023 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_XSF_H
#define DISAS_RISCV_XSF_H

#include "disas/riscv.h"

const rv_opcode_data *decode_xsf(rv_decode *, rv_isa);

#endif /* DISAS_RISCV_XSF_H */
