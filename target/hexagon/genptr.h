/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_GENPTR_H
#define HEXAGON_GENPTR_H

#include "insn.h"
#include "tcg/tcg.h"
#include "translate.h"

extern const SemanticInsn opcode_genptr[];

void gen_store32(TCGv vaddr, TCGv src, int width, uint32_t slot);
void gen_store1(TCGv_env cpu_env, TCGv vaddr, TCGv src, uint32_t slot);
void gen_store2(TCGv_env cpu_env, TCGv vaddr, TCGv src, uint32_t slot);
void gen_store4(TCGv_env cpu_env, TCGv vaddr, TCGv src, uint32_t slot);
void gen_store8(TCGv_env cpu_env, TCGv vaddr, TCGv_i64 src, uint32_t slot);
void gen_store1i(TCGv_env cpu_env, TCGv vaddr, int32_t src, uint32_t slot);
void gen_store2i(TCGv_env cpu_env, TCGv vaddr, int32_t src, uint32_t slot);
void gen_store4i(TCGv_env cpu_env, TCGv vaddr, int32_t src, uint32_t slot);
void gen_store8i(TCGv_env cpu_env, TCGv vaddr, int64_t src, uint32_t slot);
TCGv gen_read_reg(TCGv result, int num);
TCGv gen_read_preg(TCGv pred, uint8_t num);
void gen_log_reg_write(int rnum, TCGv val);
void gen_log_pred_write(DisasContext *ctx, int pnum, TCGv val);
void gen_set_usr_field(int field, TCGv val);
void gen_set_usr_fieldi(int field, int x);
void gen_set_usr_field_if(int field, TCGv val);
void gen_sat_i32(TCGv dest, TCGv source, int width);
void gen_sat_i32_ovfl(TCGv ovfl, TCGv dest, TCGv source, int width);
void gen_satu_i32(TCGv dest, TCGv source, int width);
void gen_satu_i32_ovfl(TCGv ovfl, TCGv dest, TCGv source, int width);
void gen_sat_i64(TCGv_i64 dest, TCGv_i64 source, int width);
void gen_sat_i64_ovfl(TCGv ovfl, TCGv_i64 dest, TCGv_i64 source, int width);
void gen_satu_i64(TCGv_i64 dest, TCGv_i64 source, int width);
void gen_satu_i64_ovfl(TCGv ovfl, TCGv_i64 dest, TCGv_i64 source, int width);
void gen_add_sat_i64(TCGv_i64 ret, TCGv_i64 a, TCGv_i64 b);
TCGv gen_8bitsof(TCGv result, TCGv value);
void gen_set_byte_i64(int N, TCGv_i64 result, TCGv src);
TCGv gen_get_byte(TCGv result, int N, TCGv src, bool sign);
TCGv gen_get_byte_i64(TCGv result, int N, TCGv_i64 src, bool sign);
TCGv gen_get_half(TCGv result, int N, TCGv src, bool sign);
void gen_set_half(int N, TCGv result, TCGv src);
void gen_set_half_i64(int N, TCGv_i64 result, TCGv src);
void probe_noshuf_load(TCGv va, int s, int mi);

#endif
