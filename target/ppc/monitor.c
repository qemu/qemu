/*
 * QEMU PPC (monitor definitions)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "cpu.h"

void hmp_info_tlb(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    CPUArchState *env1 = mon_get_cpu_env(mon);

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    dump_mmu(env1);
}
