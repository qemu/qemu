/*
 * QEMU monitor for m68k
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"

void hmp_info_tlb(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    CPUArchState *env1 = monitor_hmp_get_cpu_env(hmp);

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    dump_mmu(env1);
}
