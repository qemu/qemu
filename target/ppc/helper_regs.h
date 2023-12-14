/*
 *  PowerPC emulation special registers manipulation helpers for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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

#ifndef HELPER_REGS_H
#define HELPER_REGS_H

void hreg_swap_gpr_tgpr(CPUPPCState *env);
void hreg_compute_hflags(CPUPPCState *env);
void hreg_update_pmu_hflags(CPUPPCState *env);
void cpu_interrupt_exittb(CPUState *cs);
int hreg_store_msr(CPUPPCState *env, target_ulong value, int alter_hv);

#ifdef CONFIG_USER_ONLY
static inline void check_tlb_flush(CPUPPCState *env, bool global) { }
#else
void check_tlb_flush(CPUPPCState *env, bool global);
#endif

#endif /* HELPER_REGS_H */
