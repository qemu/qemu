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

static uint64_t bb_count;
static uint64_t insn_count;
static bool do_inline;

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autofree gchar *out;
    out = g_strdup_printf("bb's: %" PRIu64", insns: %" PRIu64 "\n",
                          bb_count, insn_count);
    qemu_plugin_outs(out);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    unsigned long n_insns = (unsigned long)udata;

    insn_count += n_insns;
    bb_count++;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    unsigned long n_insns = qemu_plugin_tb_n_insns(tb);

    if (do_inline) {
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &bb_count, 1);
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &insn_count, n_insns);
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
    if (argc && strcmp(argv[0], "inline") == 0) {
        do_inline = true;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
