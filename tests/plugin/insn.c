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

#define MAX_CPUS 8 /* lets not go nuts */

typedef struct {
    uint64_t insn_count;
} InstructionCount;

static InstructionCount counts[MAX_CPUS];
static uint64_t inline_insn_count;

static bool do_inline;
static bool do_size;
static GArray *sizes;

typedef struct {
    char *match_string;
    uint64_t hits[MAX_CPUS];
    uint64_t last_hit[MAX_CPUS];
    uint64_t total_delta[MAX_CPUS];
    GPtrArray *history[MAX_CPUS];
} Match;

static GArray *matches;

typedef struct {
    Match *match;
    uint64_t vaddr;
    uint64_t hits;
    char *disas;
} Instruction;

static void vcpu_insn_exec_before(unsigned int cpu_index, void *udata)
{
    unsigned int i = cpu_index % MAX_CPUS;
    InstructionCount *c = &counts[i];

    c->insn_count++;
}

static void vcpu_insn_matched_exec_before(unsigned int cpu_index, void *udata)
{
    unsigned int i = cpu_index % MAX_CPUS;
    Instruction *insn = (Instruction *) udata;
    Match *match = insn->match;
    g_autoptr(GString) ts = g_string_new("");

    insn->hits++;
    g_string_append_printf(ts, "0x%" PRIx64 ", '%s', %"PRId64 " hits",
                           insn->vaddr, insn->disas, insn->hits);

    uint64_t icount = counts[i].insn_count;
    uint64_t delta = icount - match->last_hit[i];

    match->hits[i]++;
    match->total_delta[i] += delta;

    g_string_append_printf(ts,
                           ", %"PRId64" match hits, "
                           "Δ+%"PRId64 " since last match,"
                           " %"PRId64 " avg insns/match\n",
                           match->hits[i], delta,
                           match->total_delta[i] / match->hits[i]);

    match->last_hit[i] = icount;

    qemu_plugin_outs(ts->str);

    g_ptr_array_add(match->history[i], insn);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (do_inline) {
            qemu_plugin_register_vcpu_insn_exec_inline(
                insn, QEMU_PLUGIN_INLINE_ADD_U64, &inline_insn_count, 1);
        } else {
            uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_insn_exec_before, QEMU_PLUGIN_CB_NO_REGS,
                GUINT_TO_POINTER(vaddr));
        }

        if (do_size) {
            size_t sz = qemu_plugin_insn_size(insn);
            if (sz > sizes->len) {
                g_array_set_size(sizes, sz);
            }
            unsigned long *cnt = &g_array_index(sizes, unsigned long, sz);
            (*cnt)++;
        }

        /*
         * If we are tracking certain instructions we will need more
         * information about the instruction which we also need to
         * save if there is a hit.
         */
        if (matches) {
            char *insn_disas = qemu_plugin_insn_disas(insn);
            int j;
            for (j = 0; j < matches->len; j++) {
                Match *m = &g_array_index(matches, Match, j);
                if (g_str_has_prefix(insn_disas, m->match_string)) {
                    Instruction *rec = g_new0(Instruction, 1);
                    rec->disas = g_strdup(insn_disas);
                    rec->vaddr = qemu_plugin_insn_vaddr(insn);
                    rec->match = m;
                    qemu_plugin_register_vcpu_insn_exec_cb(
                        insn, vcpu_insn_matched_exec_before,
                        QEMU_PLUGIN_CB_NO_REGS, rec);
                }
            }
            g_free(insn_disas);
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) out = g_string_new(NULL);
    int i;

    if (do_size) {
        for (i = 0; i <= sizes->len; i++) {
            unsigned long *cnt = &g_array_index(sizes, unsigned long, i);
            if (*cnt) {
                g_string_append_printf(out,
                                       "len %d bytes: %ld insns\n", i, *cnt);
            }
        }
    } else if (do_inline) {
        g_string_append_printf(out, "insns: %" PRIu64 "\n", inline_insn_count);
    } else {
        uint64_t total_insns = 0;
        for (i = 0; i < MAX_CPUS; i++) {
            InstructionCount *c = &counts[i];
            if (c->insn_count) {
                g_string_append_printf(out, "cpu %d insns: %" PRIu64 "\n",
                                       i, c->insn_count);
                total_insns += c->insn_count;
            }
        }
        g_string_append_printf(out, "total insns: %" PRIu64 "\n",
                               total_insns);
    }
    qemu_plugin_outs(out->str);
}


/* Add a match to the array of matches */
static void parse_match(char *match)
{
    Match new_match = { .match_string = match };
    int i;
    for (i = 0; i < MAX_CPUS; i++) {
        new_match.history[i] = g_ptr_array_new();
    }
    if (!matches) {
        matches = g_array_new(false, true, sizeof(Match));
    }
    g_array_append_val(matches, new_match);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "inline") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_inline)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "sizes") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_size)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "match") == 0) {
            parse_match(tokens[1]);
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (do_size) {
        sizes = g_array_new(true, true, sizeof(unsigned long));
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
