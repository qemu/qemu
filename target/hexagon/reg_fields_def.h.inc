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

/*
 * For registers that have individual fields, explain them here
 *   DEF_REG_FIELD(tag,
 *                 bit start offset,
 *                 width
 */

/* USR fields */
DEF_REG_FIELD(USR_OVF,            0, 1)
DEF_REG_FIELD(USR_FPINVF,         1, 1)
DEF_REG_FIELD(USR_FPDBZF,         2, 1)
DEF_REG_FIELD(USR_FPOVFF,         3, 1)
DEF_REG_FIELD(USR_FPUNFF,         4, 1)
DEF_REG_FIELD(USR_FPINPF,         5, 1)

DEF_REG_FIELD(USR_LPCFG,          8, 2)

DEF_REG_FIELD(USR_FPRND,         22, 2)

DEF_REG_FIELD(USR_FPINVE,        25, 1)
DEF_REG_FIELD(USR_FPDBZE,        26, 1)
DEF_REG_FIELD(USR_FPOVFE,        27, 1)
DEF_REG_FIELD(USR_FPUNFE,        28, 1)
DEF_REG_FIELD(USR_FPINPE,        29, 1)
