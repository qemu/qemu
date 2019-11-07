/*
 * SiFive CPU types
 *
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_CPU_H
#define HW_SIFIVE_CPU_H

#if defined(TARGET_RISCV32)
#define SIFIVE_E_CPU TYPE_RISCV_CPU_SIFIVE_E31
#define SIFIVE_U_CPU TYPE_RISCV_CPU_SIFIVE_U34
#elif defined(TARGET_RISCV64)
#define SIFIVE_E_CPU TYPE_RISCV_CPU_SIFIVE_E51
#define SIFIVE_U_CPU TYPE_RISCV_CPU_SIFIVE_U54
#endif

#endif /* HW_SIFIVE_CPU_H */
