/*
 * Copyright (C) 2023, Pierrick Bouvier <pierrick.bouvier@linaro.org>
 *
 * Demonstrates and tests usage of inline ops.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <stdint.h>
#include <stdio.h>

#include <qemu-plugin.h>

typedef struct {
    uint64_t count_tb;
    uint64_t count_tb_inline;
    uint64_t count_insn;
    uint64_t count_insn_inline;
    uint64_t count_mem;
    uint64_t count_mem_inline;
} CPUCount;

static struct qemu_plugin_scoreboard *counts;
static qemu_plugin_u64 count_tb;
static qemu_plugin_u64 count_tb_inline;
static qemu_plugin_u64 count_insn;
static qemu_plugin_u64 count_insn_inline;
static qemu_plugin_u64 count_mem;
static qemu_plugin_u64 count_mem_inline;

static uint64_t global_count_tb;
static uint64_t global_count_insn;
static uint64_t global_count_mem;
static unsigned int max_cpu_index;
static GMutex tb_lock;
static GMutex insn_lock;
static GMutex mem_lock;

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static void stats_insn(void)
{
    const uint64_t expected = global_count_insn;
    const uint64_t per_vcpu = qemu_plugin_u64_sum(count_insn);
    const uint64_t inl_per_vcpu =
        qemu_plugin_u64_sum(count_insn_inline);
    printf("insn: %" PRIu64 "\n", expected);
    printf("insn: %" PRIu64 " (per vcpu)\n", per_vcpu);
    printf("insn: %" PRIu64 " (per vcpu inline)\n", inl_per_vcpu);
    g_assert(expected > 0);
    g_assert(per_vcpu == expected);
    g_assert(inl_per_vcpu == expected);
}

static void stats_tb(void)
{
    const uint64_t expected = global_count_tb;
    const uint64_t per_vcpu = qemu_plugin_u64_sum(count_tb);
    const uint64_t inl_per_vcpu =
        qemu_plugin_u64_sum(count_tb_inline);
    printf("tb: %" PRIu64 "\n", expected);
    printf("tb: %" PRIu64 " (per vcpu)\n", per_vcpu);
    printf("tb: %" PRIu64 " (per vcpu inline)\n", inl_per_vcpu);
    g_assert(expected > 0);
    g_assert(per_vcpu == expected);
    g_assert(inl_per_vcpu == expected);
}

static void stats_mem(void)
{
    const uint64_t expected = global_count_mem;
    const uint64_t per_vcpu = qemu_plugin_u64_sum(count_mem);
    const uint64_t inl_per_vcpu =
        qemu_plugin_u64_sum(count_mem_inline);
    printf("mem: %" PRIu64 "\n", expected);
    printf("mem: %" PRIu64 " (per vcpu)\n", per_vcpu);
    printf("mem: %" PRIu64 " (per vcpu inline)\n", inl_per_vcpu);
    g_assert(expected > 0);
    g_assert(per_vcpu == expected);
    g_assert(inl_per_vcpu == expected);
}

static void plugin_exit(qemu_plugin_id_t id, void *udata)
{
    const unsigned int num_cpus = qemu_plugin_num_vcpus();
    g_assert(num_cpus == max_cpu_index + 1);

    for (int i = 0; i < num_cpus ; ++i) {
        const uint64_t tb = qemu_plugin_u64_get(count_tb, i);
        const uint64_t tb_inline = qemu_plugin_u64_get(count_tb_inline, i);
        const uint64_t insn = qemu_plugin_u64_get(count_insn, i);
        const uint64_t insn_inline = qemu_plugin_u64_get(count_insn_inline, i);
        const uint64_t mem = qemu_plugin_u64_get(count_mem, i);
        const uint64_t mem_inline = qemu_plugin_u64_get(count_mem_inline, i);
        printf("cpu %d: tb (%" PRIu64 ", %" PRIu64 ") | "
               "insn (%" PRIu64 ", %" PRIu64 ") | "
               "mem (%" PRIu64 ", %" PRIu64 ")"
               "\n",
               i, tb, tb_inline, insn, insn_inline, mem, mem_inline);
        g_assert(tb == tb_inline);
        g_assert(insn == insn_inline);
        g_assert(mem == mem_inline);
    }

    stats_tb();
    stats_insn();
    stats_mem();

    qemu_plugin_scoreboard_free(counts);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    qemu_plugin_u64_add(count_tb, cpu_index, 1);
    g_mutex_lock(&tb_lock);
    max_cpu_index = MAX(max_cpu_index, cpu_index);
    global_count_tb++;
    g_mutex_unlock(&tb_lock);
}

static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    qemu_plugin_u64_add(count_insn, cpu_index, 1);
    g_mutex_lock(&insn_lock);
    global_count_insn++;
    g_mutex_unlock(&insn_lock);
}

static void vcpu_mem_access(unsigned int cpu_index,
                            qemu_plugin_meminfo_t info,
                            uint64_t vaddr,
                            void *userdata)
{
    qemu_plugin_u64_add(count_mem, cpu_index, 1);
    g_mutex_lock(&mem_lock);
    global_count_mem++;
    g_mutex_unlock(&mem_lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    qemu_plugin_register_vcpu_tb_exec_cb(
        tb, vcpu_tb_exec, QEMU_PLUGIN_CB_NO_REGS, 0);
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, count_tb_inline, 1);

    for (int idx = 0; idx < qemu_plugin_tb_n_insns(tb); ++idx) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, idx);
        qemu_plugin_register_vcpu_insn_exec_cb(
            insn, vcpu_insn_exec, QEMU_PLUGIN_CB_NO_REGS, 0);
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_ADD_U64, count_insn_inline, 1);
        qemu_plugin_register_vcpu_mem_cb(insn, &vcpu_mem_access,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, 0);
        qemu_plugin_register_vcpu_mem_inline_per_vcpu(
            insn, QEMU_PLUGIN_MEM_RW,
            QEMU_PLUGIN_INLINE_ADD_U64,
            count_mem_inline, 1);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    counts = qemu_plugin_scoreboard_new(sizeof(CPUCount));
    count_tb = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, count_tb);
    count_insn = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, count_insn);
    count_mem = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, count_mem);
    count_tb_inline = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, count_tb_inline);
    count_insn_inline = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, count_insn_inline);
    count_mem_inline = qemu_plugin_scoreboard_u64_in_struct(
        counts, CPUCount, count_mem_inline);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
