/*
 *  PowerPC ISAV3 BookS emulation generic mmu helpers for qemu.
 *
 *  Copyright (c) 2017 Suraj Jitindar Singh, IBM Corporation
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "mmu-hash64.h"
#include "mmu-book3s-v3.h"
#include "mmu-radix64.h"

bool ppc64_v3_get_pate(PowerPCCPU *cpu, target_ulong lpid, ppc_v3_pate_t *entry)
{
    uint64_t patb = cpu->env.spr[SPR_PTCR] & PTCR_PATB;
    uint64_t pats = cpu->env.spr[SPR_PTCR] & PTCR_PATS;

    /* Calculate number of entries */
    pats = 1ull << (pats + 12 - 4);
    if (pats <= lpid) {
        return false;
    }

    /* Grab entry */
    patb += 16 * lpid;
    entry->dw0 = ldq_phys(CPU(cpu)->as, patb);
    entry->dw1 = ldq_phys(CPU(cpu)->as, patb + 8);
    return true;
}
