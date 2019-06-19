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
#include "qemu/ctype.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"

static target_long monitor_get_ccr(const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    unsigned int u;
    int i;

    u = 0;
    for (i = 0; i < 8; i++) {
        u |= env->crf[i] << (32 - (4 * (i + 1)));
    }

    return u;
}

static target_long monitor_get_decr(const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu(const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl(const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return cpu_ppc_load_tbl(env);
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env();

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    dump_mmu(env1);
}

const MonitorDef monitor_defs[] = {
    { "fpscr", offsetof(CPUPPCState, fpscr) },
    /* Next instruction pointer */
    { "nip|pc", offsetof(CPUPPCState, nip) },
    { "lr", offsetof(CPUPPCState, lr) },
    { "ctr", offsetof(CPUPPCState, ctr) },
    { "decr", 0, &monitor_get_decr, },
    { "ccr|cr", 0, &monitor_get_ccr, },
    /* Machine state register */
    { "xer", offsetof(CPUPPCState, xer) },
    { "msr", offsetof(CPUPPCState, msr) },
    { "tbu", 0, &monitor_get_tbu, },
    { "tbl", 0, &monitor_get_tbl, },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}

static int ppc_cpu_get_reg_num(const char *numstr, int maxnum, int *pregnum)
{
    int regnum;
    char *endptr = NULL;

    if (!*numstr) {
        return false;
    }

    regnum = strtoul(numstr, &endptr, 10);
    if (*endptr || (regnum >= maxnum)) {
        return false;
    }
    *pregnum = regnum;

    return true;
}

int target_get_monitor_def(CPUState *cs, const char *name, uint64_t *pval)
{
    int i, regnum;
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    /* General purpose registers */
    if ((qemu_tolower(name[0]) == 'r') &&
        ppc_cpu_get_reg_num(name + 1, ARRAY_SIZE(env->gpr), &regnum)) {
        *pval = env->gpr[regnum];
        return 0;
    }

    /* Floating point registers */
    if ((qemu_tolower(name[0]) == 'f') &&
        ppc_cpu_get_reg_num(name + 1, 32, &regnum)) {
        *pval = *cpu_fpr_ptr(env, regnum);
        return 0;
    }

    /* Special purpose registers */
    for (i = 0; i < ARRAY_SIZE(env->spr_cb); ++i) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (spr->name && (strcasecmp(name, spr->name) == 0)) {
            *pval = env->spr[i];
            return 0;
        }
    }

    /* Segment registers */
#if !defined(CONFIG_USER_ONLY)
    if ((strncasecmp(name, "sr", 2) == 0) &&
        ppc_cpu_get_reg_num(name + 2, ARRAY_SIZE(env->sr), &regnum)) {
        *pval = env->sr[regnum];
        return 0;
    }
#endif

    return -EINVAL;
}
