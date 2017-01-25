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

DEF_HELPER_2(raise_exception, void, env, i32)

#if !defined(CONFIG_USER_ONLY)
DEF_HELPER_2(mmu_read_debug, void, env, i32)
DEF_HELPER_3(mmu_write, void, env, i32, i32)
DEF_HELPER_1(check_interrupts, void, env)
#endif
