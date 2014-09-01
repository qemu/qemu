/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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

/* Arithmetic */
DEF_HELPER_3(add_ssov, i32, env, i32, i32)
DEF_HELPER_3(sub_ssov, i32, env, i32, i32)
/* CSA */
DEF_HELPER_2(call, void, env, i32)
DEF_HELPER_1(ret, void, env)
DEF_HELPER_2(bisr, void, env, i32)
