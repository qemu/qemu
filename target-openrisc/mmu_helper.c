/*
 * OpenRISC MMU helper routines
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

#ifndef CONFIG_USER_ONLY
#include "exec/softmmu_exec.h"

void tlb_fill(CPUState *cs, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = openrisc_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);

    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault.  */
            cpu_restore_state(cs, retaddr);
        }
        /* Raise Exception.  */
        cpu_loop_exit(cs);
    }
}
#endif
