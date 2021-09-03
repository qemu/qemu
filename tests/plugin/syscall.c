/*
 * Copyright (C) 2020, Matthias Weckbecker <matthias@weckbecker.name>
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
    int64_t num;
    int64_t calls;
    int64_t errors;
} SyscallStats;

static GMutex lock;
static GHashTable *statistics;

static SyscallStats *get_or_create_entry(int64_t num)
{
    SyscallStats *entry =
        (SyscallStats *) g_hash_table_lookup(statistics, GINT_TO_POINTER(num));

    if (!entry) {
        entry = g_new0(SyscallStats, 1);
        entry->num = num;
        g_hash_table_insert(statistics, GINT_TO_POINTER(num), (gpointer) entry);
    }

    return entry;
}

static void vcpu_syscall(qemu_plugin_id_t id, unsigned int vcpu_index,
                         int64_t num, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5,
                         uint64_t a6, uint64_t a7, uint64_t a8)
{
    if (statistics) {
        SyscallStats *entry;
        g_mutex_lock(&lock);
        entry = get_or_create_entry(num);
        entry->calls++;
        g_mutex_unlock(&lock);
    } else {
        g_autofree gchar *out = g_strdup_printf("syscall #%" PRIi64 "\n", num);
        qemu_plugin_outs(out);
    }
}

static void vcpu_syscall_ret(qemu_plugin_id_t id, unsigned int vcpu_idx,
                             int64_t num, int64_t ret)
{
    if (statistics) {
        SyscallStats *entry;

        g_mutex_lock(&lock);
        /* Should always return an existent entry. */
        entry = get_or_create_entry(num);
        if (ret < 0) {
            entry->errors++;
        }
        g_mutex_unlock(&lock);
    } else {
        g_autofree gchar *out;
        out = g_strdup_printf("syscall #%" PRIi64 " returned -> %" PRIi64 "\n",
                num, ret);
        qemu_plugin_outs(out);
    }
}

static void print_entry(gpointer val, gpointer user_data)
{
    g_autofree gchar *out;
    SyscallStats *entry = (SyscallStats *) val;
    int64_t syscall_num = entry->num;
    out = g_strdup_printf(
        "%-13" PRIi64 "%-6" PRIi64 " %" PRIi64 "\n",
        syscall_num, entry->calls, entry->errors);
    qemu_plugin_outs(out);
}

static gint comp_func(gconstpointer ea, gconstpointer eb)
{
    SyscallStats *ent_a = (SyscallStats *) ea;
    SyscallStats *ent_b = (SyscallStats *) eb;

    return ent_a->calls > ent_b->calls ? -1 : 1;
}

/* ************************************************************************* */
static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    if (!statistics) {
        return;
    }

    g_mutex_lock(&lock);
    GList *entries = g_hash_table_get_values(statistics);
    entries = g_list_sort(entries, comp_func);
    qemu_plugin_outs("syscall no.  calls  errors\n");

    g_list_foreach(entries, print_entry, NULL);

    g_list_free(entries);
    g_hash_table_destroy(statistics);
    g_mutex_unlock(&lock);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    bool do_print = false;

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "print") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_print)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
            }
        } else {
            fprintf(stderr, "unsupported argument: %s\n", argv[i]);
            return -1;
        }
    }

    if (!do_print) {
        statistics = g_hash_table_new_full(NULL, g_direct_equal, NULL, g_free);
    }

    qemu_plugin_register_vcpu_syscall_cb(id, vcpu_syscall);
    qemu_plugin_register_vcpu_syscall_ret_cb(id, vcpu_syscall_ret);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
