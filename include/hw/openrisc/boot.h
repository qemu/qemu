/*
 * QEMU OpenRISC boot helpers.
 *
 * Copyright (c) 2022 Stafford Horne <shorne@gmail.com>
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

#ifndef OPENRISC_BOOT_H
#define OPENRISC_BOOT_H

#include "exec/cpu-defs.h"

hwaddr openrisc_load_kernel(ram_addr_t ram_size,
                            const char *kernel_filename,
                            uint32_t *bootstrap_pc);

hwaddr openrisc_load_initrd(void *fdt, const char *filename,
                            hwaddr load_start, uint64_t mem_size);

uint32_t openrisc_load_fdt(void *fdt, hwaddr load_start,
                           uint64_t mem_size);

#endif /* OPENRISC_BOOT_H */
