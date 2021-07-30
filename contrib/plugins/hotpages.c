/*
 * Copyright (C) 2019, Alex Bennée <alex.bennee@linaro.org>
 *
 * Hot Pages - show which pages saw the most memory accesses.
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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static uint64_t page_size = 4096;
static uint64_t page_mask;
static int limit = 50;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;
static bool track_io;

enum sort_type {
    SORT_RW = 0,
    SORT_R,
    SORT_W,
    SORT_A
};

static int sort_by = SORT_RW;

typedef struct {
    uint64_t page_address;
    int cpu_read;
    int cpu_write;
    uint64_t reads;
    uint64_t writes;
} PageCounters;

static GMutex lock;
static GHashTable *pages;

static gint cmp_access_count(gconstpointer a, gconstpointer b)
{
    PageCounters *ea = (PageCounters *) a;
    PageCounters *eb = (PageCounters *) b;
    int r;
    switch (sort_by) {
    case SORT_RW:
        r = (ea->reads + ea->writes) > (eb->reads + eb->writes) ? -1 : 1;
        break;
    case SORT_R:
        r = ea->reads > eb->reads ? -1 : 1;
        break;
    case SORT_W:
        r = ea->writes > eb->writes ? -1 : 1;
        break;
    case SORT_A:
        r = ea->page_address > eb->page_address ? -1 : 1;
        break;
    default:
        g_assert_not_reached();
    }
    return r;
}


static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("Addr, RCPUs, Reads, WCPUs, Writes\n");
    int i;
    GList *counts;

    counts = g_hash_table_get_values(pages);
    if (counts && g_list_next(counts)) {
        GList *it;

        it = g_list_sort(counts, cmp_access_count);

        for (i = 0; i < limit && it->next; i++, it = it->next) {
            PageCounters *rec = (PageCounters *) it->data;
            g_string_append_printf(report,
                                   "0x%016"PRIx64", 0x%04x, %"PRId64
                                   ", 0x%04x, %"PRId64"\n",
                                   rec->page_address,
                                   rec->cpu_read, rec->reads,
                                   rec->cpu_write, rec->writes);
        }
        g_list_free(it);
    }

    qemu_plugin_outs(report->str);
}

static void plugin_init(void)
{
    page_mask = (page_size - 1);
    pages = g_hash_table_new(NULL, g_direct_equal);
}

static void vcpu_haddr(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                       uint64_t vaddr, void *udata)
{
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(meminfo, vaddr);
    uint64_t page;
    PageCounters *count;

    /* We only get a hwaddr for system emulation */
    if (track_io) {
        if (hwaddr && qemu_plugin_hwaddr_is_io(hwaddr)) {
            page = vaddr;
        } else {
            return;
        }
    } else {
        if (hwaddr && !qemu_plugin_hwaddr_is_io(hwaddr)) {
            page = (uint64_t) qemu_plugin_hwaddr_phys_addr(hwaddr);
        } else {
            page = vaddr;
        }
    }
    page &= ~page_mask;

    g_mutex_lock(&lock);
    count = (PageCounters *) g_hash_table_lookup(pages, GUINT_TO_POINTER(page));

    if (!count) {
        count = g_new0(PageCounters, 1);
        count->page_address = page;
        g_hash_table_insert(pages, GUINT_TO_POINTER(page), (gpointer) count);
    }
    if (qemu_plugin_mem_is_store(meminfo)) {
        count->writes++;
        count->cpu_write |= (1 << cpu_index);
    } else {
        count->reads++;
        count->cpu_read |= (1 << cpu_index);
    }

    g_mutex_unlock(&lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_haddr,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         rw, NULL);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", -1);

        if (g_strcmp0(tokens[0], "sortby") == 0) {
            if (g_strcmp0(tokens[1], "reads") == 0) {
                sort_by = SORT_R;
            } else if (g_strcmp0(tokens[1], "writes") == 0) {
                sort_by = SORT_W;
            } else if (g_strcmp0(tokens[1], "address") == 0) {
                sort_by = SORT_A;
            } else {
                fprintf(stderr, "invalid value to sortby: %s\n", tokens[1]);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "io") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &track_io)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "pagesize") == 0) {
            page_size = g_ascii_strtoull(tokens[1], NULL, 10);
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
