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
    uint64_t mem_count;
    uint64_t io_count;
} CPUCount;

typedef struct {
    uint64_t vaddr;
    const char *sym;
} InsnInfo;

static struct qemu_plugin_scoreboard *counts;
static qemu_plugin_u64 mem_count;
static qemu_plugin_u64 io_count;
static bool do_inline, do_callback, do_print_accesses;
static bool do_haddr;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

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
    qemu_plugin_scoreboard_free(counts);
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
        if (do_callback) {
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

    counts = qemu_plugin_scoreboard_new(sizeof(CPUCount));
    mem_count = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, mem_count);
    io_count = qemu_plugin_scoreboard_u64_in_struct(counts, CPUCount, io_count);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
