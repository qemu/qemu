/*
 * QEMU PPC (monitor definitions)
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
#include "qapi/qapi-commands-machine-target.h"
#include "cpu-models.h"
#include "cpu-qom.h"

static target_long monitor_get_ccr(Monitor *mon, const struct MonitorDef *md,
                                   int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    unsigned int u;

    u = ppc_get_cr(env);

    return u;
}

static target_long monitor_get_xer(Monitor *mon, const struct MonitorDef *md,
                                   int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    return cpu_read_xer(env);
}

static target_long monitor_get_decr(Monitor *mon, const struct MonitorDef *md,
                                    int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    if (!env->tb_env) {
        return 0;
    }
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu(Monitor *mon, const struct MonitorDef *md,
                                   int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    if (!env->tb_env) {
        return 0;
    }
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl(Monitor *mon, const struct MonitorDef *md,
                                   int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    if (!env->tb_env) {
        return 0;
    }
    return cpu_ppc_load_tbl(env);
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

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
    { "xer", 0, &monitor_get_xer },
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

static void ppc_cpu_defs_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **first = user_data;
    const char *typename;
    CpuDefinitionInfo *info;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen(POWERPC_CPU_TYPE_SUFFIX));

    QAPI_LIST_PREPEND(*first, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;
    int i;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    g_slist_foreach(list, ppc_cpu_defs_entry, &cpu_list);
    g_slist_free(list);

    for (i = 0; ppc_cpu_aliases[i].alias != NULL; i++) {
        PowerPCCPUAlias *alias = &ppc_cpu_aliases[i];
        ObjectClass *oc;
        CpuDefinitionInfo *info;

        oc = ppc_cpu_class_by_name(alias->model);
        if (oc == NULL) {
            continue;
        }

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(alias->alias);
        info->q_typename = g_strdup(object_class_get_name(oc));

        QAPI_LIST_PREPEND(cpu_list, info);
    }

    return cpu_list;
}
