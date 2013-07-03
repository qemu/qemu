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

#include "exec/def-helper.h"

/* exception */
DEF_HELPER_FLAGS_2(exception, 0, void, env, i32)

/* float */
DEF_HELPER_FLAGS_2(itofd, 0, i64, env, i64)
DEF_HELPER_FLAGS_2(itofs, 0, i32, env, i32)
DEF_HELPER_FLAGS_2(ftoid, 0, i64, env, i64)
DEF_HELPER_FLAGS_2(ftois, 0, i32, env, i32)

#define FOP_MADD(op)                                             \
DEF_HELPER_FLAGS_3(float_ ## op ## _s, 0, i32, env, i32, i32)    \
DEF_HELPER_FLAGS_3(float_ ## op ## _d, 0, i64, env, i64, i64)
FOP_MADD(muladd)
#undef FOP_MADD

#define FOP_CALC(op)                                            \
DEF_HELPER_FLAGS_3(float_ ## op ## _s, 0, i32, env, i32, i32)    \
DEF_HELPER_FLAGS_3(float_ ## op ## _d, 0, i64, env, i64, i64)
FOP_CALC(add)
FOP_CALC(sub)
FOP_CALC(mul)
FOP_CALC(div)
FOP_CALC(rem)
#undef FOP_CALC

#define FOP_CMP(op)                                              \
DEF_HELPER_FLAGS_3(float_ ## op ## _s, 0, i32, env, i32, i32)    \
DEF_HELPER_FLAGS_3(float_ ## op ## _d, 0, i64, env, i64, i64)
FOP_CMP(eq)
FOP_CMP(lt)
FOP_CMP(le)
FOP_CMP(ne)
FOP_CMP(gt)
FOP_CMP(ge)
#undef FOP_CMP

/* int */
DEF_HELPER_FLAGS_1(ff1, 0, tl, tl)
DEF_HELPER_FLAGS_1(fl1, 0, tl, tl)
DEF_HELPER_FLAGS_3(mul32, 0, i32, env, i32, i32)

/* interrupt */
DEF_HELPER_FLAGS_1(rfe, 0, void, env)

/* sys */
DEF_HELPER_FLAGS_4(mtspr, 0, void, env, tl, tl, tl)
DEF_HELPER_FLAGS_4(mfspr, 0, tl, env, tl, tl, tl)

#include "exec/def-helper.h"
