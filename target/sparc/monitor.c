/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "hmp.h"


void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env();

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    dump_mmu((FILE*)mon, (fprintf_function)monitor_printf, env1);
}

#ifndef TARGET_SPARC64
static target_long monitor_get_psr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();

    return cpu_get_psr(env);
}
#endif

static target_long monitor_get_reg(const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return env->regwptr[val];
}

const MonitorDef monitor_defs[] = {
    { "g0", offsetof(CPUSPARCState, gregs[0]) },
    { "g1", offsetof(CPUSPARCState, gregs[1]) },
    { "g2", offsetof(CPUSPARCState, gregs[2]) },
    { "g3", offsetof(CPUSPARCState, gregs[3]) },
    { "g4", offsetof(CPUSPARCState, gregs[4]) },
    { "g5", offsetof(CPUSPARCState, gregs[5]) },
    { "g6", offsetof(CPUSPARCState, gregs[6]) },
    { "g7", offsetof(CPUSPARCState, gregs[7]) },
    { "o0", 0, monitor_get_reg },
    { "o1", 1, monitor_get_reg },
    { "o2", 2, monitor_get_reg },
    { "o3", 3, monitor_get_reg },
    { "o4", 4, monitor_get_reg },
    { "o5", 5, monitor_get_reg },
    { "o6", 6, monitor_get_reg },
    { "o7", 7, monitor_get_reg },
    { "l0", 8, monitor_get_reg },
    { "l1", 9, monitor_get_reg },
    { "l2", 10, monitor_get_reg },
    { "l3", 11, monitor_get_reg },
    { "l4", 12, monitor_get_reg },
    { "l5", 13, monitor_get_reg },
    { "l6", 14, monitor_get_reg },
    { "l7", 15, monitor_get_reg },
    { "i0", 16, monitor_get_reg },
    { "i1", 17, monitor_get_reg },
    { "i2", 18, monitor_get_reg },
    { "i3", 19, monitor_get_reg },
    { "i4", 20, monitor_get_reg },
    { "i5", 21, monitor_get_reg },
    { "i6", 22, monitor_get_reg },
    { "i7", 23, monitor_get_reg },
    { "pc", offsetof(CPUSPARCState, pc) },
    { "npc", offsetof(CPUSPARCState, npc) },
    { "y", offsetof(CPUSPARCState, y) },
#ifndef TARGET_SPARC64
    { "psr", 0, &monitor_get_psr, },
    { "wim", offsetof(CPUSPARCState, wim) },
#endif
    { "tbr", offsetof(CPUSPARCState, tbr) },
    { "fsr", offsetof(CPUSPARCState, fsr) },
    { "f0", offsetof(CPUSPARCState, fpr[0].l.upper) },
    { "f1", offsetof(CPUSPARCState, fpr[0].l.lower) },
    { "f2", offsetof(CPUSPARCState, fpr[1].l.upper) },
    { "f3", offsetof(CPUSPARCState, fpr[1].l.lower) },
    { "f4", offsetof(CPUSPARCState, fpr[2].l.upper) },
    { "f5", offsetof(CPUSPARCState, fpr[2].l.lower) },
    { "f6", offsetof(CPUSPARCState, fpr[3].l.upper) },
    { "f7", offsetof(CPUSPARCState, fpr[3].l.lower) },
    { "f8", offsetof(CPUSPARCState, fpr[4].l.upper) },
    { "f9", offsetof(CPUSPARCState, fpr[4].l.lower) },
    { "f10", offsetof(CPUSPARCState, fpr[5].l.upper) },
    { "f11", offsetof(CPUSPARCState, fpr[5].l.lower) },
    { "f12", offsetof(CPUSPARCState, fpr[6].l.upper) },
    { "f13", offsetof(CPUSPARCState, fpr[6].l.lower) },
    { "f14", offsetof(CPUSPARCState, fpr[7].l.upper) },
    { "f15", offsetof(CPUSPARCState, fpr[7].l.lower) },
    { "f16", offsetof(CPUSPARCState, fpr[8].l.upper) },
    { "f17", offsetof(CPUSPARCState, fpr[8].l.lower) },
    { "f18", offsetof(CPUSPARCState, fpr[9].l.upper) },
    { "f19", offsetof(CPUSPARCState, fpr[9].l.lower) },
    { "f20", offsetof(CPUSPARCState, fpr[10].l.upper) },
    { "f21", offsetof(CPUSPARCState, fpr[10].l.lower) },
    { "f22", offsetof(CPUSPARCState, fpr[11].l.upper) },
    { "f23", offsetof(CPUSPARCState, fpr[11].l.lower) },
    { "f24", offsetof(CPUSPARCState, fpr[12].l.upper) },
    { "f25", offsetof(CPUSPARCState, fpr[12].l.lower) },
    { "f26", offsetof(CPUSPARCState, fpr[13].l.upper) },
    { "f27", offsetof(CPUSPARCState, fpr[13].l.lower) },
    { "f28", offsetof(CPUSPARCState, fpr[14].l.upper) },
    { "f29", offsetof(CPUSPARCState, fpr[14].l.lower) },
    { "f30", offsetof(CPUSPARCState, fpr[15].l.upper) },
    { "f31", offsetof(CPUSPARCState, fpr[15].l.lower) },
#ifdef TARGET_SPARC64
    { "f32", offsetof(CPUSPARCState, fpr[16]) },
    { "f34", offsetof(CPUSPARCState, fpr[17]) },
    { "f36", offsetof(CPUSPARCState, fpr[18]) },
    { "f38", offsetof(CPUSPARCState, fpr[19]) },
    { "f40", offsetof(CPUSPARCState, fpr[20]) },
    { "f42", offsetof(CPUSPARCState, fpr[21]) },
    { "f44", offsetof(CPUSPARCState, fpr[22]) },
    { "f46", offsetof(CPUSPARCState, fpr[23]) },
    { "f48", offsetof(CPUSPARCState, fpr[24]) },
    { "f50", offsetof(CPUSPARCState, fpr[25]) },
    { "f52", offsetof(CPUSPARCState, fpr[26]) },
    { "f54", offsetof(CPUSPARCState, fpr[27]) },
    { "f56", offsetof(CPUSPARCState, fpr[28]) },
    { "f58", offsetof(CPUSPARCState, fpr[29]) },
    { "f60", offsetof(CPUSPARCState, fpr[30]) },
    { "f62", offsetof(CPUSPARCState, fpr[31]) },
    { "asi", offsetof(CPUSPARCState, asi) },
    { "pstate", offsetof(CPUSPARCState, pstate) },
    { "cansave", offsetof(CPUSPARCState, cansave) },
    { "canrestore", offsetof(CPUSPARCState, canrestore) },
    { "otherwin", offsetof(CPUSPARCState, otherwin) },
    { "wstate", offsetof(CPUSPARCState, wstate) },
    { "cleanwin", offsetof(CPUSPARCState, cleanwin) },
    { "fprs", offsetof(CPUSPARCState, fprs) },
#endif
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}
