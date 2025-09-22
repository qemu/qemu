/*
 * Copyright (C) 2025, Pierrick Bouvier <pierrick.bouvier@linaro.org>
 *
 * Generates a trace compatible with uftrace (similar to uftrace record).
 * https://github.com/namhyung/uftrace
 *
 * See docs/about/emulation.rst|Uftrace for details and examples.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <qemu-plugin.h>
#include <glib.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct Cpu {
    GByteArray *buf;
} Cpu;

static struct qemu_plugin_scoreboard *score;

static void track_callstack(unsigned int cpu_index, void *udata)
{
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);

    for (size_t i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        uintptr_t pc = qemu_plugin_insn_vaddr(insn);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, track_callstack,
                                               QEMU_PLUGIN_CB_R_REGS,
                                               (void *) pc);
    }
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    Cpu *cpu = qemu_plugin_scoreboard_find(score, vcpu_index);
    cpu->buf = g_byte_array_new();
}

static void vcpu_end(unsigned int vcpu_index)
{
    Cpu *cpu = qemu_plugin_scoreboard_find(score, vcpu_index);
    g_byte_array_free(cpu->buf, true);
    memset(cpu, 0, sizeof(Cpu));
}

static void at_exit(qemu_plugin_id_t id, void *data)
{
    for (size_t i = 0; i < qemu_plugin_num_vcpus(); ++i) {
        vcpu_end(i);
    }

    qemu_plugin_scoreboard_free(score);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    score = qemu_plugin_scoreboard_new(sizeof(Cpu));
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    return 0;
}
