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

#include "def-helper.h"

/* exception */
DEF_HELPER_FLAGS_2(exception, 0, void, env, i32)

/* int */
DEF_HELPER_FLAGS_1(ff1, 0, tl, tl)
DEF_HELPER_FLAGS_1(fl1, 0, tl, tl)
DEF_HELPER_FLAGS_3(mul32, 0, i32, env, i32, i32)

/* interrupt */
DEF_HELPER_FLAGS_1(rfe, 0, void, env)

#include "def-helper.h"
