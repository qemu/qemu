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

/*
 * plugins should not include anything from QEMU aside from the
 * API header. However as this is a test plugin to exercise the
 * internals of QEMU and we want to avoid needless code duplication we
 * do so here. bswap.h is pretty self-contained although it needs a
 * few things provided by compiler.h.
 */
#include <compiler.h>
#include <bswap.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t mem_count;
    uint64_t io_count;
} CPUCount;

typedef struct {
    uint64_t vaddr;
    const char *sym;
} InsnInfo;

/*
 * For the "memory" system test we need to track accesses to
 * individual regions. We mirror the data written to the region and
 * then check when it is read that it matches up.
 *
 * We do this as regions rather than pages to save on complications
 * with page crossing and the fact the test only cares about the
 * test_data region.
 */
static uint64_t region_size = 4096 * 4;
static uint64_t region_mask;

typedef struct {
    uint64_t region_address;
    uint64_t reads;
    uint64_t writes;
    uint8_t *data;
    /* Did we see every write and read with correct values? */
    bool     seen_all;
} RegionInfo;

static struct qemu_plugin_scoreboard *counts;
static qemu_plugin_u64 mem_count;
static qemu_plugin_u64 io_count;
static bool do_inline, do_callback, do_print_accesses, do_region_summary;
static bool do_haddr;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;


static GMutex lock;
static GHashTable *regions;

static gint addr_order(gconstpointer a, gconstpointer b)
{
    RegionInfo *na = (RegionInfo *) a;
    RegionInfo *nb = (RegionInfo *) b;

    return na->region_address > nb->region_address ? 1 : -1;
}


static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) out = g_string_new("");

    if (do_inline || do_callback) {
        g_string_printf(out, "mem accesses: %" PRIu64 "\n",
                        qemu_plugin_u64_sum(mem_count));
    }
    if (do_haddr) {
        g_string_append_printf(out, "io accesses: %" PRIu64 "\n",
                               qemu_plugin_u64_sum(io_count));
    }
    qemu_plugin_outs(out->str);


    if (do_region_summary) {
        GList *counts = g_hash_table_get_values(regions);

        counts = g_list_sort(counts, addr_order);

        g_string_printf(out, "Region Base, Reads, Writes, Seen all\n");

        if (counts && g_list_next(counts)) {
            for (/* counts */; counts; counts = counts->next) {
                RegionInfo *ri = (RegionInfo *) counts->data;

                g_string_append_printf(out,
                                       "0x%016"PRIx64", "
                                       "%"PRId64", %"PRId64", %s\n",
                                       ri->region_address,
                                       ri->reads,
                                       ri->writes,
                                       ri->seen_all ? "true" : "false");
            }
        }
        qemu_plugin_outs(out->str);
    }

    qemu_plugin_scoreboard_free(counts);
}

/*
 * Update the region tracking info for the access. We split up accesses
 * that span regions even though the plugin infrastructure will deliver
 * it as a single access.
 */
static void update_region_info(uint64_t region, uint64_t offset,
                               qemu_plugin_meminfo_t meminfo,
                               qemu_plugin_mem_value value,
                               unsigned size)
{
    bool be = qemu_plugin_mem_is_big_endian(meminfo);
    bool is_store = qemu_plugin_mem_is_store(meminfo);
    RegionInfo *ri;
    bool unseen_data = false;

    g_assert(offset + size <= region_size);

    g_mutex_lock(&lock);
    ri = (RegionInfo *) g_hash_table_lookup(regions, &region);

    if (!ri) {
        ri = g_new0(RegionInfo, 1);
        ri->region_address = region;
        ri->data = g_malloc0(region_size);
        ri->seen_all = true;
        g_hash_table_insert(regions, &ri->region_address, ri);
    }

    if (is_store) {
        ri->writes++;
    } else {
        ri->reads++;
    }

    switch (value.type) {
    case QEMU_PLUGIN_MEM_VALUE_U8:
        if (is_store) {
            ri->data[offset] = value.data.u8;
        } else if (ri->data[offset] != value.data.u8) {
            unseen_data = true;
        }
        break;
    case QEMU_PLUGIN_MEM_VALUE_U16:
    {
        uint16_t *p = (uint16_t *) &ri->data[offset];
        if (is_store) {
            if (be) {
                stw_be_p(p, value.data.u16);
            } else {
                stw_le_p(p, value.data.u16);
            }
        } else {
            uint16_t val = be ? lduw_be_p(p) : lduw_le_p(p);
            unseen_data = val != value.data.u16;
        }
        break;
    }
    case QEMU_PLUGIN_MEM_VALUE_U32:
    {
        uint32_t *p = (uint32_t *) &ri->data[offset];
        if (is_store) {
            if (be) {
                stl_be_p(p, value.data.u32);
            } else {
                stl_le_p(p, value.data.u32);
            }
        } else {
            uint32_t val = be ? ldl_be_p(p) : ldl_le_p(p);
            unseen_data = val != value.data.u32;
        }
        break;
    }
    case QEMU_PLUGIN_MEM_VALUE_U64:
    {
        uint64_t *p = (uint64_t *) &ri->data[offset];
        if (is_store) {
            if (be) {
                stq_be_p(p, value.data.u64);
            } else {
                stq_le_p(p, value.data.u64);
            }
        } else {
            uint64_t val = be ? ldq_be_p(p) : ldq_le_p(p);
            unseen_data = val != value.data.u64;
        }
        break;
    }
    case QEMU_PLUGIN_MEM_VALUE_U128:
        /* non in test so skip */
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * This is expected for regions initialised by QEMU (.text etc) but we
     * expect to see all data read and written to the test_data region
     * of the memory test.
     */
    if (unseen_data && ri->seen_all) {
        g_autoptr(GString) error = g_string_new("Warning: ");
        g_string_append_printf(error, "0x%016"PRIx64":%"PRId64
                               " read an un-instrumented value\n",
                               region, offset);
        qemu_plugin_outs(error->str);
        ri->seen_all = false;
    }

    g_mutex_unlock(&lock);
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                     uint64_t vaddr, void *udata)
{
    if (do_haddr) {
        struct qemu_plugin_hwaddr *hwaddr;
        hwaddr = qemu_plugin_get_hwaddr(meminfo, vaddr);
        if (qemu_plugin_hwaddr_is_io(hwaddr)) {
            qemu_plugin_u64_add(io_count, cpu_index, 1);
        } else {
            qemu_plugin_u64_add(mem_count, cpu_index, 1);
        }
    } else {
        qemu_plugin_u64_add(mem_count, cpu_index, 1);
    }

    if (do_region_summary) {
        uint64_t region = vaddr & ~region_mask;
        uint64_t offset = vaddr & region_mask;
        qemu_plugin_mem_value value = qemu_plugin_mem_get_value(meminfo);
        unsigned size = 1 << qemu_plugin_mem_size_shift(meminfo);

        update_region_info(region, offset, meminfo, value, size);
    }
}

static void print_access(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                         uint64_t vaddr, void *udata)
{
    InsnInfo *insn_info = udata;
    unsigned size = 8 << qemu_plugin_mem_size_shift(meminfo);
    const char *type = qemu_plugin_mem_is_store(meminfo) ? "store" : "load";
    qemu_plugin_mem_value value = qemu_plugin_mem_get_value(meminfo);
    uint64_t hwaddr =
        qemu_plugin_hwaddr_phys_addr(qemu_plugin_get_hwaddr(meminfo, vaddr));
    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out,
                    "0x%"PRIx64",%s,0x%"PRIx64",0x%"PRIx64",%d,%s,",
                    insn_info->vaddr, insn_info->sym,
                    vaddr, hwaddr, size, type);
    switch (value.type) {
    case QEMU_PLUGIN_MEM_VALUE_U8:
        g_string_append_printf(out, "0x%02"PRIx8, value.data.u8);
        break;
    case QEMU_PLUGIN_MEM_VALUE_U16:
        g_string_append_printf(out, "0x%04"PRIx16, value.data.u16);
        break;
    case QEMU_PLUGIN_MEM_VALUE_U32:
        g_string_append_printf(out, "0x%08"PRIx32, value.data.u32);
        break;
    case QEMU_PLUGIN_MEM_VALUE_U64:
        g_string_append_printf(out, "0x%016"PRIx64, value.data.u64);
        break;
    case QEMU_PLUGIN_MEM_VALUE_U128:
        g_string_append_printf(out, "0x%016"PRIx64"%016"PRIx64,
                               value.data.u128.high, value.data.u128.low);
        break;
    default:
        g_assert_not_reached();
    }
    g_string_append_printf(out, "\n");
    qemu_plugin_outs(out->str);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (do_inline) {
            qemu_plugin_register_vcpu_mem_inline_per_vcpu(
                insn, rw,
                QEMU_PLUGIN_INLINE_ADD_U64,
                mem_count, 1);
        }
        if (do_callback || do_region_summary) {
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             rw, NULL);
        }
        if (do_print_accesses) {
            /* we leak this pointer, to avoid locking to keep track of it */
            InsnInfo *insn_info = g_malloc(sizeof(InsnInfo));
            const char *sym = qemu_plugin_insn_symbol(insn);
            insn_info->sym = sym ? sym : "";
            insn_info->vaddr = qemu_plugin_insn_vaddr(insn);
            qemu_plugin_register_vcpu_mem_cb(insn, print_access,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             rw, (void *) insn_info);
        }
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "haddr") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_haddr)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "track") == 0) {
            if (g_strcmp0(tokens[1], "r") == 0) {
                rw = QEMU_PLUGIN_MEM_R;
            } else if (g_strcmp0(tokens[1], "w") == 0) {
                rw = QEMU_PLUGIN_MEM_W;
            } else if (g_strcmp0(tokens[1], "rw") == 0) {
                rw = QEMU_PLUGIN_MEM_RW;
            } else {
                fprintf(stderr, "invalid value for argument track: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "inline") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_inline)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "callback") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_callback)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "print-accesses") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &do_print_accesses)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "region-summary") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &do_region_summary)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (do_inline && do_callback) {
        fprintf(stderr,
                "can't enable inline and callback counting at the same time\n");
        return -1;
    }

    if (do_print_accesses) {
        g_autoptr(GString) out = g_string_new("");
        g_string_printf(out,
                "insn_vaddr,insn_symbol,mem_vaddr,mem_hwaddr,"
                "access_size,access_type,mem_value\n");
        qemu_plugin_outs(out->str);
    }

    if (do_region_summary) {
        region_mask = (region_size - 1);
        regions = g_hash_table_new(g_int64_hash, g_int64_equal);
    }

    counts = qemu_plugin_scoreboard_new(sizeof(CPUCount));
    mem_count = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, mem_count);
    io_count = qemu_plugin_scoreboard_u64_in_struct(counts, CPUCount, io_count);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
