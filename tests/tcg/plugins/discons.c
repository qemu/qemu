/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025, Julian Ganz <neither@nut.email>
 *
 * This plugin exercises the discontinuity plugin API and asserts some
 * of its behaviour regarding reported program counters.
 */
#include <stdio.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

struct cpu_state {
    uint64_t last_pc;
    uint64_t from_pc;
    uint64_t next_pc;
    uint64_t has_from;
    bool has_next;
    enum qemu_plugin_discon_type next_type;
};

struct insn_data {
    uint64_t addr;
    uint64_t next_pc;
    bool next_valid;
};

static struct qemu_plugin_scoreboard *states;

static qemu_plugin_u64 last_pc;
static qemu_plugin_u64 from_pc;
static qemu_plugin_u64 has_from;

static bool abort_on_mismatch;
static bool trace_all_insns;

static bool addr_eq(uint64_t a, uint64_t b)
{
    if (a == b) {
        return true;
    }

    uint64_t a_hw;
    uint64_t b_hw;
    if (!qemu_plugin_translate_vaddr(a, &a_hw) ||
        !qemu_plugin_translate_vaddr(b, &b_hw))
    {
        return false;
    }

    return a_hw == b_hw;
}

static void report_mismatch(const char *pc_name, unsigned int vcpu_index,
                            enum qemu_plugin_discon_type type, uint64_t last,
                            uint64_t expected, uint64_t encountered)
{
    gchar *report;
    const char *discon_type_name = "unknown";

    if (addr_eq(expected, encountered)) {
        return;
    }

    switch (type) {
    case QEMU_PLUGIN_DISCON_INTERRUPT:
        discon_type_name = "interrupt";
        break;
    case QEMU_PLUGIN_DISCON_EXCEPTION:
        discon_type_name = "exception";
        break;
    case QEMU_PLUGIN_DISCON_HOSTCALL:
        discon_type_name = "hostcall";
        break;
    default:
        break;
    }

    report = g_strdup_printf("Discon %s PC mismatch on VCPU %d\n"
                             "Expected:      %"PRIx64"\nEncountered:   %"
                             PRIx64"\nExecuted Last: %"PRIx64
                             "\nEvent type:    %s\n",
                             pc_name, vcpu_index, expected, encountered, last,
                             discon_type_name);
    if (abort_on_mismatch) {
        /*
         * The qemu log infrastructure may lose messages when aborting. Using
         * fputs directly ensures the final report is visible to developers.
         */
        fputs(report, stderr);
        g_abort();
    } else {
        qemu_plugin_outs(report);
    }
    g_free(report);
}

static void vcpu_discon(qemu_plugin_id_t id, unsigned int vcpu_index,
                        enum qemu_plugin_discon_type type, uint64_t from_pc,
                        uint64_t to_pc)
{
    struct cpu_state *state = qemu_plugin_scoreboard_find(states, vcpu_index);

    if (type == QEMU_PLUGIN_DISCON_EXCEPTION &&
        addr_eq(state->last_pc, from_pc))
    {
        /*
         * For some types of exceptions, insn_exec will be called for the
         * instruction that caused the exception. This is valid behaviour and
         * does not need to be reported.
         */
    } else if (state->has_next) {
        /*
         * We may encounter discontinuity chains without any instructions
         * being executed in between.
         */
        report_mismatch("source", vcpu_index, type, state->last_pc,
                        state->next_pc, from_pc);
    } else if (state->has_from) {
        report_mismatch("source", vcpu_index, type, state->last_pc,
                        state->from_pc, from_pc);
    }

    state->has_from = false;

    state->next_pc = to_pc;
    state->next_type = type;
    state->has_next = true;
}

static void insn_exec(unsigned int vcpu_index, void *userdata)
{
    struct cpu_state *state = qemu_plugin_scoreboard_find(states, vcpu_index);

    if (state->has_next) {
        report_mismatch("target", vcpu_index, state->next_type, state->last_pc,
                        state->next_pc, state->last_pc);
        state->has_next = false;
    }

    if (trace_all_insns) {
        g_autoptr(GString) report = g_string_new(NULL);
        g_string_append_printf(report, "Exec insn at %"PRIx64" on VCPU %d\n",
                               state->last_pc, vcpu_index);
        qemu_plugin_outs(report->str);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        uint64_t next_pc = pc + qemu_plugin_insn_size(insn);
        uint64_t has_next = (i + 1) < n_insns;

        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn,
                                                            QEMU_PLUGIN_INLINE_STORE_U64,
                                                            last_pc, pc);
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn,
                                                            QEMU_PLUGIN_INLINE_STORE_U64,
                                                            from_pc, next_pc);
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn,
                                                            QEMU_PLUGIN_INLINE_STORE_U64,
                                                            has_from, has_next);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS, NULL);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (!info->system_emulation) {
        qemu_plugin_outs("Testing of the disontinuity plugin API is only"
                         " possible in system emulation mode.");
        return 0;
    }

    /* Set defaults */
    abort_on_mismatch = true;
    trace_all_insns = false;

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "abort") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &abort_on_mismatch)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "trace-all") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &trace_all_insns)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    states = qemu_plugin_scoreboard_new(sizeof(struct cpu_state));
    last_pc = qemu_plugin_scoreboard_u64_in_struct(states, struct cpu_state,
                                                   last_pc);
    from_pc = qemu_plugin_scoreboard_u64_in_struct(states, struct cpu_state,
                                                   from_pc);
    has_from = qemu_plugin_scoreboard_u64_in_struct(states, struct cpu_state,
                                                    has_from);

    qemu_plugin_register_vcpu_discon_cb(id, QEMU_PLUGIN_DISCON_ALL,
                                        vcpu_discon);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    return 0;
}
