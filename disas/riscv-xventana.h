/*
 * QEMU disassembler -- RISC-V specific header (xventana*).
 *
 * Copyright (c) 2023 VRULL GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_XVENTANA_H
#define DISAS_RISCV_XVENTANA_H

#include "disas/riscv.h"

const rv_opcode_data *decode_xventanacondops(rv_decode*, rv_isa);

#endif /* DISAS_RISCV_XVENTANA_H */
