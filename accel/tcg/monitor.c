/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  QEMU TCG monitor
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qemu/qht.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "monitor/monitor.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "system/tcg.h"
#include "tcg/tcg.h"
#include "internal-common.h"
#include "tb-context.h"


static void dump_drift_info(GString *buf)
{
    if (!icount_enabled()) {
        return;
    }

    g_string_append_printf(buf, "Host - Guest clock  %"PRIi64" ms\n",
                           (cpu_get_clock() - icount_get()) / SCALE_MS);
    if (icount_align_option) {
        g_string_append_printf(buf, "Max guest delay     %"PRIi64" ms\n",
                               -max_delay / SCALE_MS);
        g_string_append_printf(buf, "Max guest advance   %"PRIi64" ms\n",
                               max_advance / SCALE_MS);
    } else {
        g_string_append_printf(buf, "Max guest delay     NA\n");
        g_string_append_printf(buf, "Max guest advance   NA\n");
    }
}

static void dump_accel_info(GString *buf)
{
    AccelState *accel = current_accel();
    bool one_insn_per_tb = object_property_get_bool(OBJECT(accel),
                                                    "one-insn-per-tb",
                                                    &error_fatal);

    g_string_append_printf(buf, "Accelerator settings:\n");
    g_string_append_printf(buf, "one-insn-per-tb: %s\n\n",
                           one_insn_per_tb ? "on" : "off");
}

static void print_qht_statistics(struct qht_stats hst, GString *buf)
{
    uint32_t hgram_opts;
    size_t hgram_bins;
    char *hgram;

    if (!hst.head_buckets) {
        return;
    }
    g_string_append_printf(buf, "TB hash buckets     %zu/%zu "
                           "(%0.2f%% head buckets used)\n",
                           hst.used_head_buckets, hst.head_buckets,
                           (double)hst.used_head_buckets /
                           hst.head_buckets * 100);

    hgram_opts =  QDIST_PR_BORDER | QDIST_PR_LABELS;
    hgram_opts |= QDIST_PR_100X   | QDIST_PR_PERCENT;
    if (qdist_xmax(&hst.occupancy) - qdist_xmin(&hst.occupancy) == 1) {
        hgram_opts |= QDIST_PR_NODECIMAL;
    }
    hgram = qdist_pr(&hst.occupancy, 10, hgram_opts);
    g_string_append_printf(buf, "TB hash occupancy   %0.2f%% avg chain occ. "
                           "Histogram: %s\n",
                           qdist_avg(&hst.occupancy) * 100, hgram);
    g_free(hgram);

    hgram_opts = QDIST_PR_BORDER | QDIST_PR_LABELS;
    hgram_bins = qdist_xmax(&hst.chain) - qdist_xmin(&hst.chain);
    if (hgram_bins > 10) {
        hgram_bins = 10;
    } else {
        hgram_bins = 0;
        hgram_opts |= QDIST_PR_NODECIMAL | QDIST_PR_NOBINRANGE;
    }
    hgram = qdist_pr(&hst.chain, hgram_bins, hgram_opts);
    g_string_append_printf(buf, "TB hash avg chain   %0.3f buckets. "
                           "Histogram: %s\n",
                           qdist_avg(&hst.chain), hgram);
    g_free(hgram);
}

struct tb_tree_stats {
    size_t nb_tbs;
    size_t host_size;
    size_t target_size;
    size_t max_target_size;
    size_t direct_jmp_count;
    size_t direct_jmp2_count;
    size_t cross_page;
};

static gboolean tb_tree_stats_iter(gpointer key, gpointer value, gpointer data)
{
    const TranslationBlock *tb = value;
    struct tb_tree_stats *tst = data;

    tst->nb_tbs++;
    tst->host_size += tb->tc.size;
    tst->target_size += tb->size;
    if (tb->size > tst->max_target_size) {
        tst->max_target_size = tb->size;
    }
    if (tb->page_addr[1] != -1) {
        tst->cross_page++;
    }
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        tst->direct_jmp_count++;
        if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
            tst->direct_jmp2_count++;
        }
    }
    return false;
}

static void tlb_flush_counts(size_t *pfull, size_t *ppart, size_t *pelide)
{
    CPUState *cpu;
    size_t full = 0, part = 0, elide = 0;

    CPU_FOREACH(cpu) {
        full += qatomic_read(&cpu->neg.tlb.c.full_flush_count);
        part += qatomic_read(&cpu->neg.tlb.c.part_flush_count);
        elide += qatomic_read(&cpu->neg.tlb.c.elide_flush_count);
    }
    *pfull = full;
    *ppart = part;
    *pelide = elide;
}

static void tcg_dump_info(GString *buf)
{
    g_string_append_printf(buf, "[TCG profiler not compiled]\n");
}

static void dump_exec_info(GString *buf)
{
    struct tb_tree_stats tst = {};
    struct qht_stats hst;
    size_t nb_tbs, flush_full, flush_part, flush_elide;

    tcg_tb_foreach(tb_tree_stats_iter, &tst);
    nb_tbs = tst.nb_tbs;
    /* XXX: avoid using doubles ? */
    g_string_append_printf(buf, "Translation buffer state:\n");
    /*
     * Report total code size including the padding and TB structs;
     * otherwise users might think "-accel tcg,tb-size" is not honoured.
     * For avg host size we use the precise numbers from tb_tree_stats though.
     */
    g_string_append_printf(buf, "gen code size       %zu/%zu\n",
                           tcg_code_size(), tcg_code_capacity());
    g_string_append_printf(buf, "TB count            %zu\n", nb_tbs);
    g_string_append_printf(buf, "TB avg target size  %zu max=%zu bytes\n",
                           nb_tbs ? tst.target_size / nb_tbs : 0,
                           tst.max_target_size);
    g_string_append_printf(buf, "TB avg host size    %zu bytes "
                           "(expansion ratio: %0.1f)\n",
                           nb_tbs ? tst.host_size / nb_tbs : 0,
                           tst.target_size ?
                           (double)tst.host_size / tst.target_size : 0);
    g_string_append_printf(buf, "cross page TB count %zu (%zu%%)\n",
                           tst.cross_page,
                           nb_tbs ? (tst.cross_page * 100) / nb_tbs : 0);
    g_string_append_printf(buf, "direct jump count   %zu (%zu%%) "
                           "(2 jumps=%zu %zu%%)\n",
                           tst.direct_jmp_count,
                           nb_tbs ? (tst.direct_jmp_count * 100) / nb_tbs : 0,
                           tst.direct_jmp2_count,
                           nb_tbs ? (tst.direct_jmp2_count * 100) / nb_tbs : 0);

    qht_statistics_init(&tb_ctx.htable, &hst);
    print_qht_statistics(hst, buf);
    qht_statistics_destroy(&hst);

    g_string_append_printf(buf, "\nStatistics:\n");
    g_string_append_printf(buf, "TB flush count      %u\n",
                           qatomic_read(&tb_ctx.tb_flush_count));
    g_string_append_printf(buf, "TB invalidate count %u\n",
                           qatomic_read(&tb_ctx.tb_phys_invalidate_count));

    tlb_flush_counts(&flush_full, &flush_part, &flush_elide);
    g_string_append_printf(buf, "TLB full flushes    %zu\n", flush_full);
    g_string_append_printf(buf, "TLB partial flushes %zu\n", flush_part);
    g_string_append_printf(buf, "TLB elided flushes  %zu\n", flush_elide);
    tcg_dump_info(buf);
}

HumanReadableText *qmp_x_query_jit(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp, "JIT information is only available with accel=tcg");
        return NULL;
    }

    dump_accel_info(buf);
    dump_exec_info(buf);
    dump_drift_info(buf);

    return human_readable_text_from_str(buf);
}

static void tcg_dump_op_count(GString *buf)
{
    g_string_append_printf(buf, "[TCG profiler not compiled]\n");
}

HumanReadableText *qmp_x_query_opcount(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp,
                   "Opcode count information is only available with accel=tcg");
        return NULL;
    }

    tcg_dump_op_count(buf);

    return human_readable_text_from_str(buf);
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp_info_hrt("jit", qmp_x_query_jit);
    monitor_register_hmp_info_hrt("opcount", qmp_x_query_opcount);
}

type_init(hmp_tcg_register);
