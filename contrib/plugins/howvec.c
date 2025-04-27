/*
 * Copyright (C) 2019, Alex Bennée <alex.bennee@linaro.org>
 *
 * How vectorised is this code?
 *
 * Attempt to measure the amount of vectorisation that has been done
 * on some code by counting classes of instruction.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef enum {
    COUNT_CLASS,
    COUNT_INDIVIDUAL,
    COUNT_NONE
} CountType;

static int limit = 50;
static bool do_inline;
static bool verbose;

static GMutex lock;
static GHashTable *insns;

typedef struct {
    const char *class;
    const char *opt;
    uint32_t mask;
    uint32_t pattern;
    CountType what;
    qemu_plugin_u64 count;
} InsnClassExecCount;

typedef struct {
    char *insn;
    uint32_t opcode;
    qemu_plugin_u64 count;
    InsnClassExecCount *class;
} InsnExecCount;

/*
 * Matchers for classes of instructions, order is important.
 *
 * Your most precise match must be before looser matches. If no match
 * is found in the table we can create an individual entry.
 *
 * 31..28 27..24 23..20 19..16 15..12 11..8 7..4 3..0
 */
static InsnClassExecCount aarch64_insn_classes[] = {
    /* "Reserved"" */
    { "  UDEF",              "udef",   0xffff0000, 0x00000000, COUNT_NONE},
    { "  SVE",               "sve",    0x1e000000, 0x04000000, COUNT_CLASS},
    { "Reserved",            "res",    0x1e000000, 0x00000000, COUNT_CLASS},
    /* Data Processing Immediate */
    { "  PCrel addr",        "pcrel",  0x1f000000, 0x10000000, COUNT_CLASS},
    { "  Add/Sub (imm,tags)", "asit",   0x1f800000, 0x11800000, COUNT_CLASS},
    { "  Add/Sub (imm)",     "asi",    0x1f000000, 0x11000000, COUNT_CLASS},
    { "  Logical (imm)",     "logi",   0x1f800000, 0x12000000, COUNT_CLASS},
    { "  Move Wide (imm)",   "movwi",  0x1f800000, 0x12800000, COUNT_CLASS},
    { "  Bitfield",          "bitf",   0x1f800000, 0x13000000, COUNT_CLASS},
    { "  Extract",           "extr",   0x1f800000, 0x13800000, COUNT_CLASS},
    { "Data Proc Imm",       "dpri",   0x1c000000, 0x10000000, COUNT_CLASS},
    /* Branches */
    { "  Cond Branch (imm)", "cndb",   0xfe000000, 0x54000000, COUNT_CLASS},
    { "  Exception Gen",     "excp",   0xff000000, 0xd4000000, COUNT_CLASS},
    { "    NOP",             "nop",    0xffffffff, 0xd503201f, COUNT_NONE},
    { "  Hints",             "hint",   0xfffff000, 0xd5032000, COUNT_CLASS},
    { "  Barriers",          "barr",   0xfffff000, 0xd5033000, COUNT_CLASS},
    { "  PSTATE",            "psta",   0xfff8f000, 0xd5004000, COUNT_CLASS},
    { "  System Insn",       "sins",   0xffd80000, 0xd5080000, COUNT_CLASS},
    { "  System Reg",        "sreg",   0xffd00000, 0xd5100000, COUNT_CLASS},
    { "  Branch (reg)",      "breg",   0xfe000000, 0xd6000000, COUNT_CLASS},
    { "  Branch (imm)",      "bimm",   0x7c000000, 0x14000000, COUNT_CLASS},
    { "  Cmp & Branch",      "cmpb",   0x7e000000, 0x34000000, COUNT_CLASS},
    { "  Tst & Branch",      "tstb",   0x7e000000, 0x36000000, COUNT_CLASS},
    { "Branches",            "branch", 0x1c000000, 0x14000000, COUNT_CLASS},
    /* Loads and Stores */
    { "  AdvSimd ldstmult",  "advlsm", 0xbfbf0000, 0x0c000000, COUNT_CLASS},
    { "  AdvSimd ldstmult++", "advlsmp", 0xbfb00000, 0x0c800000, COUNT_CLASS},
    { "  AdvSimd ldst",      "advlss", 0xbf9f0000, 0x0d000000, COUNT_CLASS},
    { "  AdvSimd ldst++",    "advlssp", 0xbf800000, 0x0d800000, COUNT_CLASS},
    { "  ldst excl",         "ldstx",  0x3f000000, 0x08000000, COUNT_CLASS},
    { "    Prefetch",        "prfm",   0xff000000, 0xd8000000, COUNT_CLASS},
    { "  Load Reg (lit)",    "ldlit",  0x1b000000, 0x18000000, COUNT_CLASS},
    { "  ldst noalloc pair", "ldstnap", 0x3b800000, 0x28000000, COUNT_CLASS},
    { "  ldst pair",         "ldstp",  0x38000000, 0x28000000, COUNT_CLASS},
    { "  ldst reg",          "ldstr",  0x3b200000, 0x38000000, COUNT_CLASS},
    { "  Atomic ldst",       "atomic", 0x3b200c00, 0x38200000, COUNT_CLASS},
    { "  ldst reg (reg off)", "ldstro", 0x3b200b00, 0x38200800, COUNT_CLASS},
    { "  ldst reg (pac)",    "ldstpa", 0x3b200200, 0x38200800, COUNT_CLASS},
    { "  ldst reg (imm)",    "ldsti",  0x3b000000, 0x39000000, COUNT_CLASS},
    { "Loads & Stores",      "ldst",   0x0a000000, 0x08000000, COUNT_CLASS},
    /* Data Processing Register */
    { "Data Proc Reg",       "dprr",   0x0e000000, 0x0a000000, COUNT_CLASS},
    /* Scalar FP */
    { "Scalar FP ",          "fpsimd", 0x0e000000, 0x0e000000, COUNT_CLASS},
    /* Unclassified */
    { "Unclassified",        "unclas", 0x00000000, 0x00000000, COUNT_CLASS},
};

static InsnClassExecCount sparc32_insn_classes[] = {
    { "Call",                "call",   0xc0000000, 0x40000000, COUNT_CLASS},
    { "Branch ICond",        "bcc",    0xc1c00000, 0x00800000, COUNT_CLASS},
    { "Branch Fcond",        "fbcc",   0xc1c00000, 0x01800000, COUNT_CLASS},
    { "SetHi",               "sethi",  0xc1c00000, 0x01000000, COUNT_CLASS},
    { "FPU ALU",             "fpu",    0xc1f00000, 0x81a00000, COUNT_CLASS},
    { "ALU",                 "alu",    0xc0000000, 0x80000000, COUNT_CLASS},
    { "Load/Store",          "ldst",   0xc0000000, 0xc0000000, COUNT_CLASS},
    /* Unclassified */
    { "Unclassified",        "unclas", 0x00000000, 0x00000000, COUNT_INDIVIDUAL},
};

static InsnClassExecCount sparc64_insn_classes[] = {
    { "SetHi & Branches",     "op0",   0xc0000000, 0x00000000, COUNT_CLASS},
    { "Call",                 "op1",   0xc0000000, 0x40000000, COUNT_CLASS},
    { "Arith/Logical/Move",   "op2",   0xc0000000, 0x80000000, COUNT_CLASS},
    { "Arith/Logical/Move",   "op3",   0xc0000000, 0xc0000000, COUNT_CLASS},
    /* Unclassified */
    { "Unclassified",        "unclas", 0x00000000, 0x00000000, COUNT_INDIVIDUAL},
};

/* Default matcher for currently unclassified architectures */
static InsnClassExecCount default_insn_classes[] = {
    { "Unclassified",        "unclas", 0x00000000, 0x00000000, COUNT_INDIVIDUAL},
};

typedef struct {
    const char *qemu_target;
    InsnClassExecCount *table;
    int table_sz;
} ClassSelector;

static ClassSelector class_tables[] = {
    { "aarch64", aarch64_insn_classes, ARRAY_SIZE(aarch64_insn_classes) },
    { "sparc",   sparc32_insn_classes, ARRAY_SIZE(sparc32_insn_classes) },
    { "sparc64", sparc64_insn_classes, ARRAY_SIZE(sparc64_insn_classes) },
    { NULL, default_insn_classes, ARRAY_SIZE(default_insn_classes) },
};

static InsnClassExecCount *class_table;
static int class_table_sz;

static gint cmp_exec_count(gconstpointer a, gconstpointer b, gpointer d)
{
    InsnExecCount *ea = (InsnExecCount *) a;
    InsnExecCount *eb = (InsnExecCount *) b;
    uint64_t count_a = qemu_plugin_u64_sum(ea->count);
    uint64_t count_b = qemu_plugin_u64_sum(eb->count);
    return count_a > count_b ? -1 : 1;
}

static void free_record(gpointer data)
{
    InsnExecCount *rec = (InsnExecCount *) data;
    qemu_plugin_scoreboard_free(rec->count.score);
    g_free(rec->insn);
    g_free(rec);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("Instruction Classes:\n");
    int i;
    uint64_t total_count;
    GList *counts;
    InsnClassExecCount *class = NULL;

    for (i = 0; i < class_table_sz; i++) {
        class = &class_table[i];
        switch (class->what) {
        case COUNT_CLASS:
            total_count = qemu_plugin_u64_sum(class->count);
            if (total_count || verbose) {
                g_string_append_printf(report,
                                       "Class: %-24s\t(%" PRId64 " hits)\n",
                                       class->class,
                                       total_count);
            }
            break;
        case COUNT_INDIVIDUAL:
            g_string_append_printf(report, "Class: %-24s\tcounted individually\n",
                                   class->class);
            break;
        case COUNT_NONE:
            g_string_append_printf(report, "Class: %-24s\tnot counted\n",
                                   class->class);
            break;
        default:
            break;
        }
    }

    counts = g_hash_table_get_values(insns);
    if (counts && g_list_next(counts)) {
        g_string_append_printf(report, "Individual Instructions:\n");
        counts = g_list_sort_with_data(counts, cmp_exec_count, NULL);

        for (i = 0; i < limit && g_list_next(counts);
             i++, counts = g_list_next(counts)) {
            InsnExecCount *rec = (InsnExecCount *) counts->data;
            g_string_append_printf(report,
                                   "Instr: %-24s\t(%" PRId64 " hits)"
                                   "\t(op=0x%08x/%s)\n",
                                   rec->insn,
                                   qemu_plugin_u64_sum(rec->count),
                                   rec->opcode,
                                   rec->class ?
                                   rec->class->class : "un-categorised");
        }
        g_list_free(counts);
    }

    g_hash_table_destroy(insns);
    for (i = 0; i < ARRAY_SIZE(class_tables); i++) {
        for (int j = 0; j < class_tables[i].table_sz; ++j) {
            qemu_plugin_scoreboard_free(class_tables[i].table[j].count.score);
        }
    }


    qemu_plugin_outs(report->str);
}

static void plugin_init(void)
{
    insns = g_hash_table_new_full(NULL, g_direct_equal, NULL, &free_record);
}

static void vcpu_insn_exec_before(unsigned int cpu_index, void *udata)
{
    struct qemu_plugin_scoreboard *score = udata;
    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(score), cpu_index, 1);
}

static struct qemu_plugin_scoreboard *find_counter(
    struct qemu_plugin_insn *insn)
{
    int i;
    uint64_t *cnt = NULL;
    uint32_t opcode = 0;
    /* if opcode is greater than 32 bits, we should refactor insn hash table. */
    G_STATIC_ASSERT(sizeof(opcode) == sizeof(uint32_t));
    InsnClassExecCount *class = NULL;

    /*
     * We only match the first 32 bits of the instruction which is
     * fine for most RISCs but a bit limiting for CISC architectures.
     * They would probably benefit from a more tailored plugin.
     * However we can fall back to individual instruction counting.
     */
    qemu_plugin_insn_data(insn, &opcode, sizeof(opcode));

    for (i = 0; !cnt && i < class_table_sz; i++) {
        class = &class_table[i];
        uint32_t masked_bits = opcode & class->mask;
        if (masked_bits == class->pattern) {
            break;
        }
    }

    g_assert(class);

    switch (class->what) {
    case COUNT_NONE:
        return NULL;
    case COUNT_CLASS:
        return class->count.score;
    case COUNT_INDIVIDUAL:
    {
        InsnExecCount *icount;

        g_mutex_lock(&lock);
        icount = (InsnExecCount *) g_hash_table_lookup(insns,
                                                       (gpointer)(intptr_t) opcode);

        if (!icount) {
            icount = g_new0(InsnExecCount, 1);
            icount->opcode = opcode;
            icount->insn = qemu_plugin_insn_disas(insn);
            icount->class = class;
            struct qemu_plugin_scoreboard *score =
                qemu_plugin_scoreboard_new(sizeof(uint64_t));
            icount->count = qemu_plugin_scoreboard_u64(score);

            g_hash_table_insert(insns, (gpointer)(intptr_t) opcode, icount);
        }
        g_mutex_unlock(&lock);

        return icount->count.score;
    }
    default:
        g_assert_not_reached();
    }

    return NULL;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        struct qemu_plugin_scoreboard *cnt = find_counter(insn);

        if (cnt) {
            if (do_inline) {
                qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
                    insn, QEMU_PLUGIN_INLINE_ADD_U64,
                    qemu_plugin_scoreboard_u64(cnt), 1);
            } else {
                qemu_plugin_register_vcpu_insn_exec_cb(
                    insn, vcpu_insn_exec_before, QEMU_PLUGIN_CB_NO_REGS, cnt);
            }
        }
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(class_tables); i++) {
        for (int j = 0; j < class_tables[i].table_sz; ++j) {
            struct qemu_plugin_scoreboard *score =
                qemu_plugin_scoreboard_new(sizeof(uint64_t));
            class_tables[i].table[j].count = qemu_plugin_scoreboard_u64(score);
        }
    }

    /* Select a class table appropriate to the guest architecture */
    for (i = 0; i < ARRAY_SIZE(class_tables); i++) {
        ClassSelector *entry = &class_tables[i];
        if (!entry->qemu_target ||
            strcmp(entry->qemu_target, info->target_name) == 0) {
            class_table = entry->table;
            class_table_sz = entry->table_sz;
            break;
        }
    }

    for (i = 0; i < argc; i++) {
        char *p = argv[i];
        g_auto(GStrv) tokens = g_strsplit(p, "=", -1);
        if (g_strcmp0(tokens[0], "inline") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_inline)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", p);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "verbose") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &verbose)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", p);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "count") == 0) {
            char *value = tokens[1];
            int j;
            CountType type = COUNT_INDIVIDUAL;
            if (*value == '!') {
                type = COUNT_NONE;
                value++;
            }
            for (j = 0; j < class_table_sz; j++) {
                if (strcmp(value, class_table[j].opt) == 0) {
                    class_table[j].what = type;
                    break;
                }
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", p);
            return -1;
        }
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
