/*
 * QEMU monitor for m68k
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/hmp-target.h"
#include "monitor/monitor.h"

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env();

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    dump_mmu(env1);
}

static const MonitorDef monitor_defs[] = {
    { "d0", offsetof(CPUM68KState, dregs[0]) },
    { "d1", offsetof(CPUM68KState, dregs[1]) },
    { "d2", offsetof(CPUM68KState, dregs[2]) },
    { "d3", offsetof(CPUM68KState, dregs[3]) },
    { "d4", offsetof(CPUM68KState, dregs[4]) },
    { "d5", offsetof(CPUM68KState, dregs[5]) },
    { "d6", offsetof(CPUM68KState, dregs[6]) },
    { "d7", offsetof(CPUM68KState, dregs[7]) },
    { "a0", offsetof(CPUM68KState, aregs[0]) },
    { "a1", offsetof(CPUM68KState, aregs[1]) },
    { "a2", offsetof(CPUM68KState, aregs[2]) },
    { "a3", offsetof(CPUM68KState, aregs[3]) },
    { "a4", offsetof(CPUM68KState, aregs[4]) },
    { "a5", offsetof(CPUM68KState, aregs[5]) },
    { "a6", offsetof(CPUM68KState, aregs[6]) },
    { "a7", offsetof(CPUM68KState, aregs[7]) },
    { "pc", offsetof(CPUM68KState, pc) },
    { "sr", offsetof(CPUM68KState, sr) },
    { "ssp", offsetof(CPUM68KState, sp[0]) },
    { "usp", offsetof(CPUM68KState, sp[1]) },
    { "isp", offsetof(CPUM68KState, sp[2]) },
    { "sfc", offsetof(CPUM68KState, sfc) },
    { "dfc", offsetof(CPUM68KState, dfc) },
    { "urp", offsetof(CPUM68KState, mmu.urp) },
    { "srp", offsetof(CPUM68KState, mmu.srp) },
    { "dttr0", offsetof(CPUM68KState, mmu.ttr[M68K_DTTR0]) },
    { "dttr1", offsetof(CPUM68KState, mmu.ttr[M68K_DTTR1]) },
    { "ittr0", offsetof(CPUM68KState, mmu.ttr[M68K_ITTR0]) },
    { "ittr1", offsetof(CPUM68KState, mmu.ttr[M68K_ITTR1]) },
    { "mmusr", offsetof(CPUM68KState, mmu.mmusr) },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}
