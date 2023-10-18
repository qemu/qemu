/*
 * riscv TCG cpu class initialization
 *
 * Copyright (c) 2023 Ventana Micro Systems Inc.
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

#ifndef RISCV_TCG_CPU_H
#define RISCV_TCG_CPU_H

#include "cpu.h"

void riscv_cpu_validate_set_extensions(RISCVCPU *cpu, Error **errp);
void riscv_tcg_cpu_finalize_features(RISCVCPU *cpu, Error **errp);

#endif
