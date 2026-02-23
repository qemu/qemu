/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef X86_EMU_H
#define X86_EMU_H

#include "x86.h"
#include "x86_decode.h"
#include "x86_mmu.h"
#include "cpu.h"

struct x86_emul_ops {
    MMUTranslateResult (*mmu_gva_to_gpa) (CPUState *cpu, target_ulong gva, uint64_t *gpa, MMUTranslateFlags flags);
    void (*read_segment_descriptor)(CPUState *cpu, struct x86_segment_descriptor *desc,
                                    enum X86Seg seg);
    void (*handle_io)(CPUState *cpu, uint16_t port, void *data, int direction,
                      int size, int count);
    void (*simulate_rdmsr)(CPUState *cs);
    void (*simulate_wrmsr)(CPUState *cs);
};

extern const struct x86_emul_ops *emul_ops;

void init_emu(const struct x86_emul_ops *ops);
bool exec_instruction(CPUX86State *env, struct x86_decode *ins);
void x86_emul_raise_exception(CPUX86State *env, int exception_index, int error_code);

target_ulong read_reg(CPUX86State *env, int reg, int size);
void write_reg(CPUX86State *env, int reg, target_ulong val, int size);
target_ulong read_val_from_reg(void *reg_ptr, int size);
void write_val_to_reg(void *reg_ptr, target_ulong val, int size);
bool write_val_ext(CPUX86State *env, struct x86_decode_op *decode, target_ulong val, int size);
uint8_t *read_mmio(CPUX86State *env, target_ulong ptr, int bytes);
bool read_val_ext(CPUX86State *env, struct x86_decode_op *decode, int size, target_ulong* val);

bool exec_movzx(CPUX86State *env, struct x86_decode *decode);
bool exec_shl(CPUX86State *env, struct x86_decode *decode);
bool exec_movsx(CPUX86State *env, struct x86_decode *decode);
bool exec_ror(CPUX86State *env, struct x86_decode *decode);
bool exec_rol(CPUX86State *env, struct x86_decode *decode);
bool exec_rcl(CPUX86State *env, struct x86_decode *decode);
bool exec_rcr(CPUX86State *env, struct x86_decode *decode);
#endif
