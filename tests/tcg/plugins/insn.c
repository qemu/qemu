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
static bool do_trace;
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

/* A hash table to hold matched instructions */
static GHashTable *match_insn_records;
static GMutex match_hash_lock;


static Instruction * get_insn_record(const char *disas, uint64_t vaddr, Match *m)
{
    g_autofree char *str_hash = g_strdup_printf("%"PRIx64" %s", vaddr, disas);
    Instruction *record;

    g_mutex_lock(&match_hash_lock);

    if (!match_insn_records) {
        match_insn_records = g_hash_table_new(g_str_hash, g_str_equal);
    }

    record = g_hash_table_lookup(match_insn_records, str_hash);

    if (!record) {
        g_autoptr(GString) ts = g_string_new(str_hash);

        record = g_new0(Instruction, 1);
        record->disas = g_strdup(disas);
        record->vaddr = vaddr;
        record->match = m;

        g_hash_table_insert(match_insn_records, str_hash, record);

        g_string_prepend(ts, "Created record for: ");
        g_string_append(ts, "\n");
        qemu_plugin_outs(ts->str);
    }

    g_mutex_unlock(&match_hash_lock);

    return record;
}

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

    insn->hits++;

    uint64_t icount = qemu_plugin_u64_get(insn_count, cpu_index);
    uint64_t delta = icount - match->last_hit;

    match->hits++;
    match->total_delta += delta;
    match->last_hit = icount;

    if (do_trace) {
        g_autoptr(GString) ts = g_string_new("");
        g_string_append_printf(ts, "0x%" PRIx64 ", '%s', %"PRId64 " hits",
                               insn->vaddr, insn->disas, insn->hits);
        g_string_append_printf(ts,
                               " , cpu %u,"
                               " %"PRId64" match hits,"
                               " Δ+%"PRId64 " since last match,"
                               " %"PRId64 " avg insns/match\n",
                               cpu_index,
                               match->hits, delta,
                               match->total_delta / match->hits);

        qemu_plugin_outs(ts->str);
    }
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
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_insn_exec_before, QEMU_PLUGIN_CB_NO_REGS, NULL);
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
         *
         * We only want one record for each occurrence of the matched
         * instruction.
         */
        if (matches->len) {
            char *insn_disas = qemu_plugin_insn_disas(insn);
            for (int j = 0; j < matches->len; j++) {
                Match *m = &g_array_index(matches, Match, j);
                if (g_str_has_prefix(insn_disas, m->match_string)) {
                    Instruction *rec = get_insn_record(insn_disas,
                                                       qemu_plugin_insn_vaddr(insn),
                                                       m);

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

    g_mutex_lock(&match_hash_lock);

    for (i = 0; i < matches->len; ++i) {
        Match *m = &g_array_index(matches, Match, i);
        GHashTableIter iter;
        Instruction *record;
        qemu_plugin_u64 hit_e = qemu_plugin_scoreboard_u64_in_struct(m->counts, MatchCount, hits);
        uint64_t hits = qemu_plugin_u64_sum(hit_e);

        g_string_printf(out, "Match: %s, hits %"PRId64"\n", m->match_string, hits);
        qemu_plugin_outs(out->str);

        g_hash_table_iter_init(&iter, match_insn_records);
        while (g_hash_table_iter_next(&iter, NULL, (void **)&record)) {
            if (record->match == m) {
                g_string_printf(out,
                                "  %"PRIx64": %s (hits %"PRId64")\n",
                                record->vaddr,
                                record->disas,
                                record->hits);
                qemu_plugin_outs(out->str);
            }
        }

        g_free(m->match_string);
        qemu_plugin_scoreboard_free(m->counts);
    }

    g_mutex_unlock(&match_hash_lock);

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
        } else if (g_strcmp0(tokens[0], "trace") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_trace)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
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
