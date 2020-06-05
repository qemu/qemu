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

static uint64_t mem_count;
static uint64_t io_count;
static bool do_inline;
static bool do_haddr;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) out = g_string_new("");

    g_string_printf(out, "mem accesses: %" PRIu64 "\n", mem_count);
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
            mem_count++;
        }
    } else {
        mem_count++;
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
                                                 &mem_count, 1);
        } else {
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
    if (argc) {
        if (argc >= 3) {
            if (!strcmp(argv[2], "haddr")) {
                do_haddr = true;
            }
        }
        if (argc >= 2) {
            const char *str = argv[1];

            if (!strcmp(str, "r")) {
                rw = QEMU_PLUGIN_MEM_R;
            } else if (!strcmp(str, "w")) {
                rw = QEMU_PLUGIN_MEM_W;
            }
        }
        if (!strcmp(argv[0], "inline")) {
            do_inline = true;
        }
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
