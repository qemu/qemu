/*
 *  PowerPC ISAV3 BookS emulation generic mmu helpers for qemu.
 *
 *  Copyright (c) 2017 Suraj Jitindar Singh, IBM Corporation
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "mmu-hash64.h"
#include "mmu-book3s-v3.h"
#include "mmu-radix64.h"

int ppc64_v3_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                              int mmu_idx)
{
    if (ppc64_radix_guest(cpu)) { /* Guest uses radix */
        return ppc_radix64_handle_mmu_fault(cpu, eaddr, rwx, mmu_idx);
    } else { /* Guest uses hash */
        return ppc_hash64_handle_mmu_fault(cpu, eaddr, rwx, mmu_idx);
    }
}
