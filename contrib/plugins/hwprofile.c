/*
 * Copyright (C) 2020, Alex Bennée <alex.bennee@linaro.org>
 *
 * HW Profile - breakdown access patterns for IO to devices
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

typedef struct {
    uint64_t cpu_read;
    uint64_t cpu_write;
    uint64_t reads;
    uint64_t writes;
} IOCounts;

typedef struct {
    uint64_t off_or_pc;
    IOCounts counts;
} IOLocationCounts;

typedef struct {
    const char *name;
    uint64_t   base;
    IOCounts   totals;
    GHashTable *detail;
} DeviceCounts;

static GMutex lock;
static GHashTable *devices;

/* track the access pattern to a piece of HW */
static bool pattern;
/* track the source address of access to HW */
static bool source;
/* track only matched regions of HW */
static bool check_match;
static gchar **matches;

static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static inline bool track_reads(void)
{
    return rw == QEMU_PLUGIN_MEM_RW || rw == QEMU_PLUGIN_MEM_R;
}

static inline bool track_writes(void)
{
    return rw == QEMU_PLUGIN_MEM_RW || rw == QEMU_PLUGIN_MEM_W;
}

static void plugin_init(void)
{
    devices = g_hash_table_new(NULL, NULL);
}

static gint sort_cmp(gconstpointer a, gconstpointer b)
{
    DeviceCounts *ea = (DeviceCounts *) a;
    DeviceCounts *eb = (DeviceCounts *) b;
    return ea->totals.reads + ea->totals.writes >
           eb->totals.reads + eb->totals.writes ? -1 : 1;
}

static gint sort_loc(gconstpointer a, gconstpointer b)
{
    IOLocationCounts *ea = (IOLocationCounts *) a;
    IOLocationCounts *eb = (IOLocationCounts *) b;
    return ea->off_or_pc > eb->off_or_pc;
}

static void fmt_iocount_record(GString *s, IOCounts *rec)
{
    if (track_reads()) {
        g_string_append_printf(s, ", %"PRIx64", %"PRId64,
                               rec->cpu_read, rec->reads);
    }
    if (track_writes()) {
        g_string_append_printf(s, ", %"PRIx64", %"PRId64,
                               rec->cpu_write, rec->writes);
    }
}

static void fmt_dev_record(GString *s, DeviceCounts *rec)
{
    g_string_append_printf(s, "%s, 0x%"PRIx64,
                           rec->name, rec->base);
    fmt_iocount_record(s, &rec->totals);
    g_string_append_c(s, '\n');
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");
    GList *counts;

    if (!(pattern || source)) {
        g_string_printf(report, "Device, Address");
        if (track_reads()) {
            g_string_append_printf(report, ", RCPUs, Reads");
        }
        if (track_writes()) {
            g_string_append_printf(report, ",  WCPUs, Writes");
        }
        g_string_append_c(report, '\n');
    }

    counts = g_hash_table_get_values(devices);
    if (counts && g_list_next(counts)) {
        GList *it;

        it = g_list_sort(counts, sort_cmp);

        while (it) {
            DeviceCounts *rec = (DeviceCounts *) it->data;
            if (rec->detail) {
                GList *accesses = g_hash_table_get_values(rec->detail);
                GList *io_it = g_list_sort(accesses, sort_loc);
                const char *prefix = pattern ? "off" : "pc";
                g_string_append_printf(report, "%s @ 0x%"PRIx64"\n",
                                       rec->name, rec->base);
                while (io_it) {
                    IOLocationCounts *loc = (IOLocationCounts *) io_it->data;
                    g_string_append_printf(report, "  %s:%08"PRIx64,
                                           prefix, loc->off_or_pc);
                    fmt_iocount_record(report, &loc->counts);
                    g_string_append_c(report, '\n');
                    io_it = io_it->next;
                }
            } else {
                fmt_dev_record(report, rec);
            }
            it = it->next;
        };
        g_list_free(it);
    }

    qemu_plugin_outs(report->str);
}

static DeviceCounts *new_count(const char *name, uint64_t base)
{
    DeviceCounts *count = g_new0(DeviceCounts, 1);
    count->name = name;
    count->base = base;
    if (pattern || source) {
        count->detail = g_hash_table_new(NULL, NULL);
    }
    g_hash_table_insert(devices, (gpointer) name, count);
    return count;
}

static IOLocationCounts *new_location(GHashTable *table, uint64_t off_or_pc)
{
    IOLocationCounts *loc = g_new0(IOLocationCounts, 1);
    loc->off_or_pc = off_or_pc;
    g_hash_table_insert(table, (gpointer) off_or_pc, loc);
    return loc;
}

static void hwprofile_match_hit(DeviceCounts *rec, uint64_t off)
{
    g_autoptr(GString) report = g_string_new("hwprofile: match @ offset");
    g_string_append_printf(report, "%"PRIx64", previous hits\n", off);
    fmt_dev_record(report, rec);
    qemu_plugin_outs(report->str);
}

static void inc_count(IOCounts *count, bool is_write, unsigned int cpu_index)
{
    if (is_write) {
        count->writes++;
        count->cpu_write |= (1 << cpu_index);
    } else {
        count->reads++;
        count->cpu_read |= (1 << cpu_index);
    }
}

static void vcpu_haddr(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                       uint64_t vaddr, void *udata)
{
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(meminfo, vaddr);

    if (!hwaddr || !qemu_plugin_hwaddr_is_io(hwaddr)) {
        return;
    } else {
        const char *name = qemu_plugin_hwaddr_device_name(hwaddr);
        uint64_t off = qemu_plugin_hwaddr_phys_addr(hwaddr);
        bool is_write = qemu_plugin_mem_is_store(meminfo);
        DeviceCounts *counts;

        g_mutex_lock(&lock);
        counts = (DeviceCounts *) g_hash_table_lookup(devices, name);

        if (!counts) {
            uint64_t base = vaddr - off;
            counts = new_count(name, base);
        }

        if (check_match) {
            if (g_strv_contains((const char * const *)matches, counts->name)) {
                hwprofile_match_hit(counts, off);
                inc_count(&counts->totals, is_write, cpu_index);
            }
        } else {
            inc_count(&counts->totals, is_write, cpu_index);
        }

        /* either track offsets or source of access */
        if (source) {
            off = (uint64_t) udata;
        }

        if (pattern || source) {
            IOLocationCounts *io_count = g_hash_table_lookup(counts->detail,
                                                             (gpointer) off);
            if (!io_count) {
                io_count = new_location(counts->detail, off);
            }
            inc_count(&io_count->counts, is_write, cpu_index);
        }

        g_mutex_unlock(&lock);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        gpointer udata = (gpointer) (source ? qemu_plugin_insn_vaddr(insn) : 0);
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_haddr,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         rw, udata);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    int i;
    g_autoptr(GString) matches_raw = g_string_new("");

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "track") == 0) {
            if (g_strcmp0(tokens[1], "read") == 0) {
                rw = QEMU_PLUGIN_MEM_R;
            } else if (g_strcmp0(tokens[1], "write") == 0) {
                rw = QEMU_PLUGIN_MEM_W;
            } else {
                fprintf(stderr, "invalid value for track: %s\n", tokens[1]);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "pattern") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &pattern)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "source") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &source)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "match") == 0) {
            check_match = true;
            g_string_append_printf(matches_raw, "%s,", tokens[1]);
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }
    if (check_match) {
        matches = g_strsplit(matches_raw->str, ",", -1);
    }

    if (source && pattern) {
        fprintf(stderr, "can only currently track either source or pattern.\n");
        return -1;
    }

    if (!info->system_emulation) {
        fprintf(stderr, "hwprofile: plugin only useful for system emulation\n");
        return -1;
    }

    /* Just warn about overflow */
    if (info->system.smp_vcpus > 64 ||
        info->system.max_vcpus > 64) {
        fprintf(stderr, "hwprofile: can only track up to 64 CPUs\n");
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
