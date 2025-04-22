/*
 * Copyright (C) 2019, Alex Bennée <alex.bennee@linaro.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static bool do_inline;

/* Plugins need to take care of their own locking */
static GMutex lock;
static GHashTable *hotblocks;
static guint64 limit = 20;

/*
 * Counting Structure
 *
 * The internals of the TCG are not exposed to plugins so we can only
 * get the starting PC for each block. We cheat this slightly by
 * checking the number of instructions as well to help
 * differentiate.
 */
typedef struct {
    uint64_t start_addr;
    struct qemu_plugin_scoreboard *exec_count;
    int trans_count;
    unsigned long insns;
} ExecCount;

static gint cmp_exec_count(gconstpointer a, gconstpointer b, gpointer d)
{
    ExecCount *ea = (ExecCount *) a;
    ExecCount *eb = (ExecCount *) b;
    uint64_t count_a =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(ea->exec_count));
    uint64_t count_b =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(eb->exec_count));
    return count_a > count_b ? -1 : 1;
}

static guint exec_count_hash(gconstpointer v)
{
    const ExecCount *e = v;
    return e->start_addr ^ e->insns;
}

static gboolean exec_count_equal(gconstpointer v1, gconstpointer v2)
{
    const ExecCount *ea = v1;
    const ExecCount *eb = v2;
    return (ea->start_addr == eb->start_addr) &&
           (ea->insns == eb->insns);
}

static void exec_count_free(gpointer key, gpointer value, gpointer user_data)
{
    ExecCount *cnt = value;
    qemu_plugin_scoreboard_free(cnt->exec_count);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("collected ");
    GList *counts, *it;
    int i;

    g_string_append_printf(report, "%d entries in the hash table\n",
                           g_hash_table_size(hotblocks));
    counts = g_hash_table_get_values(hotblocks);
    it = g_list_sort_with_data(counts, cmp_exec_count, NULL);

    if (it) {
        g_string_append_printf(report, "pc, tcount, icount, ecount\n");

        for (i = 0; i < limit && it->next; i++, it = it->next) {
            ExecCount *rec = (ExecCount *) it->data;
            g_string_append_printf(
                report, "0x%016"PRIx64", %d, %ld, %"PRId64"\n",
                rec->start_addr, rec->trans_count,
                rec->insns,
                qemu_plugin_u64_sum(
                    qemu_plugin_scoreboard_u64(rec->exec_count)));
        }

        g_list_free(it);
    }

    qemu_plugin_outs(report->str);

    g_hash_table_foreach(hotblocks, exec_count_free, NULL);
    g_hash_table_destroy(hotblocks);
}

static void plugin_init(void)
{
    hotblocks = g_hash_table_new(exec_count_hash, exec_count_equal);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    ExecCount *cnt = (ExecCount *)udata;
    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(cnt->exec_count),
                        cpu_index, 1);
}

/*
 * When do_inline we ask the plugin to increment the counter for us.
 * Otherwise a helper is inserted which calls the vcpu_tb_exec
 * callback.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    ExecCount *cnt;
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t insns = qemu_plugin_tb_n_insns(tb);

    g_mutex_lock(&lock);
    {
        ExecCount e;
        e.start_addr = pc;
        e.insns = insns;
        cnt = (ExecCount *) g_hash_table_lookup(hotblocks, &e);
    }

    if (cnt) {
        cnt->trans_count++;
    } else {
        cnt = g_new0(ExecCount, 1);
        cnt->start_addr = pc;
        cnt->trans_count = 1;
        cnt->insns = insns;
        cnt->exec_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
        g_hash_table_insert(hotblocks, cnt, cnt);
    }

    g_mutex_unlock(&lock);

    if (do_inline) {
        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
            tb, QEMU_PLUGIN_INLINE_ADD_U64,
            qemu_plugin_scoreboard_u64(cnt->exec_count), 1);
    } else {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)cnt);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "inline") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_inline)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
