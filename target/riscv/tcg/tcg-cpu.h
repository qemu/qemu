/*
 * riscv TCG cpu class initialization
 *
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_TCG_CPU_H
#define RISCV_TCG_CPU_H

#include "cpu.h"

void riscv_cpu_validate_set_extensions(RISCVCPU *cpu, Error **errp);
void riscv_tcg_cpu_finalize_features(RISCVCPU *cpu, Error **errp);
bool riscv_cpu_tcg_compatible(RISCVCPU *cpu);

extern const TCGCPUOps riscv_tcg_ops;

struct DisasContext;
struct RISCVCPUConfig;
typedef struct RISCVDecoder {
    bool (*guard_func)(const struct RISCVCPUConfig *);
    bool (*riscv_cpu_decode_fn)(struct DisasContext *, uint32_t);
} RISCVDecoder;

typedef bool (*riscv_cpu_decode_fn)(struct DisasContext *, uint32_t);

extern const size_t decoder_table_size;

extern const RISCVDecoder decoder_table[];

void riscv_tcg_cpu_finalize_dynamic_decoder(RISCVCPU *cpu);

#endif
