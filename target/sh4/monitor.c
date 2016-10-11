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

static void print_tlb(Monitor *mon, int idx, tlb_t *tlb)
{
    monitor_printf(mon, " tlb%i:\t"
                   "asid=%hhu vpn=%x\tppn=%x\tsz=%hhu size=%u\t"
                   "v=%hhu shared=%hhu cached=%hhu prot=%hhu "
                   "dirty=%hhu writethrough=%hhu\n",
                   idx,
                   tlb->asid, tlb->vpn, tlb->ppn, tlb->sz, tlb->size,
                   tlb->v, tlb->sh, tlb->c, tlb->pr,
                   tlb->d, tlb->wt);
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env = mon_get_cpu_env();
    int i;

    monitor_printf (mon, "ITLB:\n");
    for (i = 0 ; i < ITLB_SIZE ; i++)
        print_tlb (mon, i, &env->itlb[i]);
    monitor_printf (mon, "UTLB:\n");
    for (i = 0 ; i < UTLB_SIZE ; i++)
        print_tlb (mon, i, &env->utlb[i]);
}
