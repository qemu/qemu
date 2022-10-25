/*
 * PMU emulation helpers for TCG IBM POWER chips
 *
 *  Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef POWER8_PMU_H
#define POWER8_PMU_H

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

#define PMC_COUNTER_NEGATIVE_VAL 0x80000000UL

void cpu_ppc_pmu_init(CPUPPCState *env);
void pmu_update_summaries(CPUPPCState *env);
#else
static inline void cpu_ppc_pmu_init(CPUPPCState *env) { }
static inline void pmu_update_summaries(CPUPPCState *env) { }
#endif

#endif
