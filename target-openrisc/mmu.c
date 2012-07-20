/*
 * OpenRISC MMU.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
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

#include "cpu.h"
#include "qemu-common.h"
#include "gdbstub.h"
#include "host-utils.h"
#ifndef CONFIG_USER_ONLY
#include "hw/loader.h"
#endif

#ifndef CONFIG_USER_ONLY
target_phys_addr_t cpu_get_phys_page_debug(CPUOpenRISCState *env,
                                           target_ulong addr)
{
    return addr;
}

void cpu_openrisc_mmu_init(OpenRISCCPU *cpu)
{
}
#endif
