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

#ifndef POWER8_PMU
#define POWER8_PMU

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
void cpu_ppc_pmu_init(CPUPPCState *env);
void pmu_update_summaries(CPUPPCState *env);
#else
static inline void cpu_ppc_pmu_init(CPUPPCState *env) { }
static inline void pmu_update_summaries(CPUPPCState *env) { }
#endif

#endif
