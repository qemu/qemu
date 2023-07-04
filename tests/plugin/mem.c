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

static uint64_t inline_mem_count;
static uint64_t cb_mem_count;
static uint64_t io_count;
static bool do_inline, do_callback;
static bool do_haddr;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) out = g_string_new("");

    if (do_inline) {
        g_string_printf(out, "inline mem accesses: %" PRIu64 "\n", inline_mem_count);
    }
    if (do_callback) {
        g_string_append_printf(out, "callback mem accesses: %" PRIu64 "\n", cb_mem_count);
    }
    if (do_haddr) {
        g_string_append_printf(out, "io accesses: %" PRIu64 "\n", io_count);
    }
    qemu_plugin_outs(out->str);
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                     uint64_t vaddr, void *udata)
{
    if (do_haddr) {
        struct qemu_plugin_hwaddr *hwaddr;
        hwaddr = qemu_plugin_get_hwaddr(meminfo, vaddr);
        if (qemu_plugin_hwaddr_is_io(hwaddr)) {
            io_count++;
        } else {
            cb_mem_count++;
        }
    } else {
        cb_mem_count++;
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (do_inline) {
            qemu_plugin_register_vcpu_mem_inline(insn, rw,
                                                 QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &inline_mem_count, 1);
        }
        if (do_callback) {
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             rw, NULL);
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
                fprintf(stderr, "invaild value for argument track: %s\n", opt);
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
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
