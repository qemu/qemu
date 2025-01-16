/*
 * Copyright (C) 2024, Simon Hamelin <simon.hamelin@grenoble-inp.org>
 *
 * Stop execution once a given address is reached or if the
 * count of executed instructions reached a specified limit
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* Scoreboard to track executed instructions count */
typedef struct {
    uint64_t insn_count;
    uint64_t current_pc;
} InstructionsCount;
static struct qemu_plugin_scoreboard *insn_count_sb;
static qemu_plugin_u64 insn_count;
static qemu_plugin_u64 current_pc;

static uint64_t icount;
static int icount_exit_code;

static bool exit_on_icount;
static bool exit_on_address;

/* Map trigger addresses to exit code */
static GHashTable *addrs_ht;

typedef struct {
    uint64_t exit_addr;
    int exit_code;
} ExitInfo;

static void exit_emulation(int return_code, char *message)
{
    qemu_plugin_outs(message);
    g_free(message);
    exit(return_code);
}

static void exit_icount_reached(unsigned int cpu_index, void *udata)
{
    uint64_t insn_vaddr = qemu_plugin_u64_get(current_pc, cpu_index);
    char *msg = g_strdup_printf("icount reached at 0x%" PRIx64 ", exiting\n",
                                insn_vaddr);
    exit_emulation(icount_exit_code, msg);
}

static void exit_address_reached(unsigned int cpu_index, void *udata)
{
    ExitInfo *ei = udata;
    g_assert(ei);
    char *msg = g_strdup_printf("0x%" PRIx64 " reached, exiting\n", ei->exit_addr);
    exit_emulation(ei->exit_code, msg);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t tb_n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < tb_n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);

        if (exit_on_icount) {
            /* Increment and check scoreboard for each instruction */
            qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
                insn, QEMU_PLUGIN_INLINE_ADD_U64, insn_count, 1);
            qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
                insn, QEMU_PLUGIN_INLINE_STORE_U64, current_pc, insn_vaddr);
            qemu_plugin_register_vcpu_insn_exec_cond_cb(
                insn, exit_icount_reached, QEMU_PLUGIN_CB_NO_REGS,
                QEMU_PLUGIN_COND_EQ, insn_count, icount + 1, NULL);
        }

        if (exit_on_address) {
            ExitInfo *ei = g_hash_table_lookup(addrs_ht, &insn_vaddr);
            if (ei) {
                /* Exit triggered by address */
                qemu_plugin_register_vcpu_insn_exec_cb(
                    insn, exit_address_reached, QEMU_PLUGIN_CB_NO_REGS, ei);
            }
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_hash_table_destroy(addrs_ht);
    qemu_plugin_scoreboard_free(insn_count_sb);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    addrs_ht = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL, g_free);

    insn_count_sb = qemu_plugin_scoreboard_new(sizeof(InstructionsCount));
    insn_count = qemu_plugin_scoreboard_u64_in_struct(
        insn_count_sb, InstructionsCount, insn_count);
    current_pc = qemu_plugin_scoreboard_u64_in_struct(
        insn_count_sb, InstructionsCount, current_pc);

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "icount") == 0) {
            g_auto(GStrv) icount_tokens = g_strsplit(tokens[1], ":", 2);
            icount = g_ascii_strtoull(icount_tokens[0], NULL, 0);
            if (icount < 1 || g_strrstr(icount_tokens[0], "-") != NULL) {
                fprintf(stderr,
                        "icount parsing failed: '%s' must be a positive "
                        "integer\n",
                        icount_tokens[0]);
                return -1;
            }
            if (icount_tokens[1]) {
                icount_exit_code = g_ascii_strtoull(icount_tokens[1], NULL, 0);
            }
            exit_on_icount = true;
        } else if (g_strcmp0(tokens[0], "addr") == 0) {
            g_auto(GStrv) addr_tokens = g_strsplit(tokens[1], ":", 2);
            ExitInfo *ei = g_malloc(sizeof(ExitInfo));
            ei->exit_addr = g_ascii_strtoull(addr_tokens[0], NULL, 0);
            ei->exit_code = 0;
            if (addr_tokens[1]) {
                ei->exit_code = g_ascii_strtoull(addr_tokens[1], NULL, 0);
            }
            g_hash_table_insert(addrs_ht, &ei->exit_addr, ei);
            exit_on_address = true;
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (!exit_on_icount && !exit_on_address) {
        fprintf(stderr, "'icount' or 'addr' argument missing\n");
        return -1;
    }

    /* Register translation block and exit callbacks */
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
