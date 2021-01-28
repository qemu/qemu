/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    GMutex lock;
    int index;
    uint64_t bb_count;
    uint64_t insn_count;
} CPUCount;

/* Used by the inline & linux-user counts */
static bool do_inline;
static CPUCount inline_count;

/* Dump running CPU total on idle? */
static bool idle_report;
static GPtrArray *counts;
static int max_cpus;

static void gen_one_cpu_report(CPUCount *count, GString *report)
{
    if (count->bb_count) {
        g_string_append_printf(report, "CPU%d: "
                               "bb's: %" PRIu64", insns: %" PRIu64 "\n",
                               count->index,
                               count->bb_count, count->insn_count);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");

    if (do_inline || !max_cpus) {
        g_string_printf(report, "bb's: %" PRIu64", insns: %" PRIu64 "\n",
                        inline_count.bb_count, inline_count.insn_count);
    } else {
        g_ptr_array_foreach(counts, (GFunc) gen_one_cpu_report, report);
    }
    qemu_plugin_outs(report->str);
}

static void vcpu_idle(qemu_plugin_id_t id, unsigned int cpu_index)
{
    CPUCount *count = g_ptr_array_index(counts, cpu_index);
    g_autoptr(GString) report = g_string_new("");
    gen_one_cpu_report(count, report);

    if (report->len > 0) {
        g_string_prepend(report, "Idling ");
        qemu_plugin_outs(report->str);
    }
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    CPUCount *count = max_cpus ?
        g_ptr_array_index(counts, cpu_index) : &inline_count;

    uintptr_t n_insns = (uintptr_t)udata;
    g_mutex_lock(&count->lock);
    count->insn_count += n_insns;
    count->bb_count++;
    g_mutex_unlock(&count->lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);

    if (do_inline) {
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &inline_count.bb_count, 1);
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &inline_count.insn_count,
                                                 n_insns);
    } else {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)n_insns);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_strcmp0(opt, "inline") == 0) {
            do_inline = true;
        } else if (g_strcmp0(opt, "idle") == 0) {
            idle_report = true;
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (info->system_emulation && !do_inline) {
        max_cpus = info->system.max_vcpus;
        counts = g_ptr_array_new();
        for (i = 0; i < max_cpus; i++) {
            CPUCount *count = g_new0(CPUCount, 1);
            g_mutex_init(&count->lock);
            count->index = i;
            g_ptr_array_add(counts, count);
        }
    } else if (!do_inline) {
        g_mutex_init(&inline_count.lock);
    }

    if (idle_report) {
        qemu_plugin_register_vcpu_idle_cb(id, vcpu_idle);
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
