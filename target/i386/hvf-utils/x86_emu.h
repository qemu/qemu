/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __X86_EMU_H__
#define __X86_EMU_H__

#include "x86.h"
#include "x86_decode.h"
#include "cpu.h"

void init_emu(void);
bool exec_instruction(struct CPUX86State *env, struct x86_decode *ins);

void load_regs(struct CPUState *cpu);
void store_regs(struct CPUState *cpu);

void simulate_rdmsr(struct CPUState *cpu);
void simulate_wrmsr(struct CPUState *cpu);

addr_t read_reg(CPUX86State *env, int reg, int size);
void write_reg(CPUX86State *env, int reg, addr_t val, int size);
addr_t read_val_from_reg(addr_t reg_ptr, int size);
void write_val_to_reg(addr_t reg_ptr, addr_t val, int size);
void write_val_ext(struct CPUX86State *env, addr_t ptr, addr_t val, int size);
uint8_t *read_mmio(struct CPUX86State *env, addr_t ptr, int bytes);
addr_t read_val_ext(struct CPUX86State *env, addr_t ptr, int size);

void exec_movzx(struct CPUX86State *env, struct x86_decode *decode);
void exec_shl(struct CPUX86State *env, struct x86_decode *decode);
void exec_movsx(struct CPUX86State *env, struct x86_decode *decode);
void exec_ror(struct CPUX86State *env, struct x86_decode *decode);
void exec_rol(struct CPUX86State *env, struct x86_decode *decode);
void exec_rcl(struct CPUX86State *env, struct x86_decode *decode);
void exec_rcr(struct CPUX86State *env, struct x86_decode *decode);
#endif
