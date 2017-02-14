/*
 * OpenRISC helper defines
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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

/* exception */
DEF_HELPER_FLAGS_2(exception, TCG_CALL_NO_WG, void, env, i32)
DEF_HELPER_FLAGS_1(ove_cy, TCG_CALL_NO_WG, void, env)
DEF_HELPER_FLAGS_1(ove_ov, TCG_CALL_NO_WG, void, env)
DEF_HELPER_FLAGS_1(ove_cyov, TCG_CALL_NO_WG, void, env)

/* float */
DEF_HELPER_FLAGS_2(itofd, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(itofs, TCG_CALL_NO_WG, i32, env, i32)
DEF_HELPER_FLAGS_2(ftoid, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(ftois, TCG_CALL_NO_WG, i32, env, i32)

DEF_HELPER_FLAGS_4(float_madd_s, TCG_CALL_NO_WG, i32, env, i32, i32, i32)
DEF_HELPER_FLAGS_4(float_madd_d, TCG_CALL_NO_WG, i64, env, i64, i64, i64)

#define FOP_CALC(op)                                            \
DEF_HELPER_FLAGS_3(float_ ## op ## _s, TCG_CALL_NO_WG, i32, env, i32, i32) \
DEF_HELPER_FLAGS_3(float_ ## op ## _d, TCG_CALL_NO_WG, i64, env, i64, i64)
FOP_CALC(add)
FOP_CALC(sub)
FOP_CALC(mul)
FOP_CALC(div)
FOP_CALC(rem)
#undef FOP_CALC

#define FOP_CMP(op)                                              \
DEF_HELPER_FLAGS_3(float_ ## op ## _s, TCG_CALL_NO_WG, i32, env, i32, i32) \
DEF_HELPER_FLAGS_3(float_ ## op ## _d, TCG_CALL_NO_WG, i64, env, i64, i64)
FOP_CMP(eq)
FOP_CMP(lt)
FOP_CMP(le)
FOP_CMP(ne)
FOP_CMP(gt)
FOP_CMP(ge)
#undef FOP_CMP

/* interrupt */
DEF_HELPER_FLAGS_1(rfe, 0, void, env)

/* sys */
DEF_HELPER_FLAGS_4(mtspr, 0, void, env, tl, tl, tl)
DEF_HELPER_FLAGS_4(mfspr, TCG_CALL_NO_WG, tl, env, tl, tl, tl)
