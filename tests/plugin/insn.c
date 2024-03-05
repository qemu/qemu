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

static qemu_plugin_u64 insn_count;

static bool do_inline;
static bool do_size;
static GArray *sizes;

typedef struct {
    uint64_t hits;
    uint64_t last_hit;
    uint64_t total_delta;
} MatchCount;

typedef struct {
    char *match_string;
    struct qemu_plugin_scoreboard *counts; /* MatchCount */
} Match;

static GArray *matches;

typedef struct {
    Match *match;
    uint64_t vaddr;
    uint64_t hits;
    char *disas;
} Instruction;

/*
 * Initialise a new vcpu with reading the register list
 */
static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();
    g_autoptr(GByteArray) reg_value = g_byte_array_new();

    if (reg_list) {
        for (int i = 0; i < reg_list->len; i++) {
            qemu_plugin_reg_descriptor *rd = &g_array_index(
                reg_list, qemu_plugin_reg_descriptor, i);
            int count = qemu_plugin_read_register(rd->handle, reg_value);
            g_assert(count > 0);
        }
    }
}


static void vcpu_insn_exec_before(unsigned int cpu_index, void *udata)
{
    qemu_plugin_u64_add(insn_count, cpu_index, 1);
}

static void vcpu_insn_matched_exec_before(unsigned int cpu_index, void *udata)
{
    Instruction *insn = (Instruction *) udata;
    Match *insn_match = insn->match;
    MatchCount *match = qemu_plugin_scoreboard_find(insn_match->counts,
                                                    cpu_index);

    g_autoptr(GString) ts = g_string_new("");

    insn->hits++;
    g_string_append_printf(ts, "0x%" PRIx64 ", '%s', %"PRId64 " hits",
                           insn->vaddr, insn->disas, insn->hits);

    uint64_t icount = qemu_plugin_u64_get(insn_count, cpu_index);
    uint64_t delta = icount - match->last_hit;

    match->hits++;
    match->total_delta += delta;

    g_string_append_printf(ts,
                           " , cpu %u,"
                           " %"PRId64" match hits,"
                           " Δ+%"PRId64 " since last match,"
                           " %"PRId64 " avg insns/match\n",
                           cpu_index,
                           match->hits, delta,
                           match->total_delta / match->hits);

    match->last_hit = icount;

    qemu_plugin_outs(ts->str);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (do_inline) {
            qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
                insn, QEMU_PLUGIN_INLINE_ADD_U64, insn_count, 1);
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
        if (matches->len) {
            char *insn_disas = qemu_plugin_insn_disas(insn);
            for (int j = 0; j < matches->len; j++) {
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
    } else {
        for (i = 0; i < qemu_plugin_num_vcpus(); i++) {
            g_string_append_printf(out, "cpu %d insns: %" PRIu64 "\n",
                                   i, qemu_plugin_u64_get(insn_count, i));
        }
        g_string_append_printf(out, "total insns: %" PRIu64 "\n",
                               qemu_plugin_u64_sum(insn_count));
    }
    qemu_plugin_outs(out->str);

    qemu_plugin_scoreboard_free(insn_count.score);
    for (i = 0; i < matches->len; ++i) {
        Match *m = &g_array_index(matches, Match, i);
        g_free(m->match_string);
        qemu_plugin_scoreboard_free(m->counts);
    }
    g_array_free(matches, TRUE);
    g_array_free(sizes, TRUE);
}


/* Add a match to the array of matches */
static void parse_match(char *match)
{
    Match new_match = {
        .match_string = g_strdup(match),
        .counts = qemu_plugin_scoreboard_new(sizeof(MatchCount)) };
    g_array_append_val(matches, new_match);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    matches = g_array_new(false, true, sizeof(Match));
    /* null terminated so 0 is not a special case */
    sizes = g_array_new(true, true, sizeof(unsigned long));

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

    insn_count = qemu_plugin_scoreboard_u64(
        qemu_plugin_scoreboard_new(sizeof(uint64_t)));

    /* Register init, translation block and exit callbacks */
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
