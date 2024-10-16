/*
 * Instructions Per Second (IPS) rate limiting plugin.
 *
 * This plugin can be used to restrict the execution of a system to a
 * particular number of Instructions Per Second (IPS). This controls
 * time as seen by the guest so while wall-clock time may be longer
 * from the guests point of view time will pass at the normal rate.
 *
 * This uses the new plugin API which allows the plugin to control
 * system time.
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <glib.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* how many times do we update time per sec */
#define NUM_TIME_UPDATE_PER_SEC 10
#define NSEC_IN_ONE_SEC (1000 * 1000 * 1000)

static GMutex global_state_lock;

static uint64_t max_insn_per_second = 1000 * 1000 * 1000; /* ips per core, per second */
static uint64_t max_insn_per_quantum; /* trap every N instructions */
static int64_t virtual_time_ns; /* last set virtual time */

static const void *time_handle;

typedef struct {
    uint64_t total_insn;
    uint64_t quantum_insn; /* insn in last quantum */
    int64_t last_quantum_time; /* time when last quantum started */
} vCPUTime;

struct qemu_plugin_scoreboard *vcpus;

/* return epoch time in ns */
static int64_t now_ns(void)
{
    return g_get_real_time() * 1000;
}

static uint64_t num_insn_during(int64_t elapsed_ns)
{
    double num_secs = elapsed_ns / (double) NSEC_IN_ONE_SEC;
    return num_secs * (double) max_insn_per_second;
}

static int64_t time_for_insn(uint64_t num_insn)
{
    double num_secs = (double) num_insn / (double) max_insn_per_second;
    return num_secs * (double) NSEC_IN_ONE_SEC;
}

static void update_system_time(vCPUTime *vcpu)
{
    int64_t elapsed_ns = now_ns() - vcpu->last_quantum_time;
    uint64_t max_insn = num_insn_during(elapsed_ns);

    if (vcpu->quantum_insn >= max_insn) {
        /* this vcpu ran faster than expected, so it has to sleep */
        uint64_t insn_advance = vcpu->quantum_insn - max_insn;
        uint64_t time_advance_ns = time_for_insn(insn_advance);
        int64_t sleep_us = time_advance_ns / 1000;
        g_usleep(sleep_us);
    }

    vcpu->total_insn += vcpu->quantum_insn;
    vcpu->quantum_insn = 0;
    vcpu->last_quantum_time = now_ns();

    /* based on total number of instructions, what should be the new time? */
    int64_t new_virtual_time = time_for_insn(vcpu->total_insn);

    g_mutex_lock(&global_state_lock);

    /* Time only moves forward. Another vcpu might have updated it already. */
    if (new_virtual_time > virtual_time_ns) {
        qemu_plugin_update_ns(time_handle, new_virtual_time);
        virtual_time_ns = new_virtual_time;
    }

    g_mutex_unlock(&global_state_lock);
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int cpu_index)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    vcpu->total_insn = 0;
    vcpu->quantum_insn = 0;
    vcpu->last_quantum_time = now_ns();
}

static void vcpu_exit(qemu_plugin_id_t id, unsigned int cpu_index)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    update_system_time(vcpu);
}

static void every_quantum_insn(unsigned int cpu_index, void *udata)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    g_assert(vcpu->quantum_insn >= max_insn_per_quantum);
    update_system_time(vcpu);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    qemu_plugin_u64 quantum_insn =
        qemu_plugin_scoreboard_u64_in_struct(vcpus, vCPUTime, quantum_insn);
    /* count (and eventually trap) once per tb */
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, quantum_insn, n_insns);
    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, every_quantum_insn,
        QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_COND_GE,
        quantum_insn, max_insn_per_quantum, NULL);
}

static void plugin_exit(qemu_plugin_id_t id, void *udata)
{
    qemu_plugin_scoreboard_free(vcpus);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "ips") == 0) {
            max_insn_per_second = g_ascii_strtoull(tokens[1], NULL, 10);
            if (!max_insn_per_second && errno) {
                fprintf(stderr, "%s: couldn't parse %s (%s)\n",
                        __func__, tokens[1], g_strerror(errno));
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    vcpus = qemu_plugin_scoreboard_new(sizeof(vCPUTime));
    max_insn_per_quantum = max_insn_per_second / NUM_TIME_UPDATE_PER_SEC;

    if (max_insn_per_quantum == 0) {
        fprintf(stderr, "minimum of %d instructions per second needed\n",
                NUM_TIME_UPDATE_PER_SEC);
        return -1;
    }

    time_handle = qemu_plugin_request_time_control();
    g_assert(time_handle);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_exit_cb(id, vcpu_exit);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
