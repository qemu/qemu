/*
 * Altera Nios II helper routines header.
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

DEF_HELPER_FLAGS_2(raise_exception, TCG_CALL_NO_WG, noreturn, env, i32)
DEF_HELPER_FLAGS_3(divs, TCG_CALL_NO_WG, s32, env, s32, s32)
DEF_HELPER_FLAGS_3(divu, TCG_CALL_NO_WG, i32, env, i32, i32)

#if !defined(CONFIG_USER_ONLY)
DEF_HELPER_3(eret, noreturn, env, i32, i32)
DEF_HELPER_FLAGS_2(rdprs, TCG_CALL_NO_WG, i32, env, i32)
DEF_HELPER_3(wrprs, void, env, i32, i32)
DEF_HELPER_2(mmu_write_tlbacc, void, env, i32)
DEF_HELPER_2(mmu_write_tlbmisc, void, env, i32)
DEF_HELPER_2(mmu_write_pteaddr, void, env, i32)
#endif
