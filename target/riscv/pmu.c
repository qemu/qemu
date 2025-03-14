/*
 * RISC-V PMU file.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "pmu.h"
#include "exec/icount.h"
#include "system/device_tree.h"

#define RISCV_TIMEBASE_FREQ 1000000000 /* 1Ghz */

/*
 * To keep it simple, any event can be mapped to any programmable counters in
 * QEMU. The generic cycle & instruction count events can also be monitored
 * using programmable counters. In that case, mcycle & minstret must continue
 * to provide the correct value as well. Heterogeneous PMU per hart is not
 * supported yet. Thus, number of counters are same across all harts.
 */
void riscv_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name)
{
    uint32_t fdt_event_ctr_map[15] = {};

   /*
    * The event encoding is specified in the SBI specification
    * Event idx is a 20bits wide number encoded as follows:
    * event_idx[19:16] = type
    * event_idx[15:0] = code
    * The code field in cache events are encoded as follows:
    * event_idx.code[15:3] = cache_id
    * event_idx.code[2:1] = op_id
    * event_idx.code[0:0] = result_id
    */

   /* SBI_PMU_HW_CPU_CYCLES: 0x01 : type(0x00) */
   fdt_event_ctr_map[0] = cpu_to_be32(0x00000001);
   fdt_event_ctr_map[1] = cpu_to_be32(0x00000001);
   fdt_event_ctr_map[2] = cpu_to_be32(cmask | 1 << 0);

   /* SBI_PMU_HW_INSTRUCTIONS: 0x02 : type(0x00) */
   fdt_event_ctr_map[3] = cpu_to_be32(0x00000002);
   fdt_event_ctr_map[4] = cpu_to_be32(0x00000002);
   fdt_event_ctr_map[5] = cpu_to_be32(cmask | 1 << 2);

   /* SBI_PMU_HW_CACHE_DTLB : 0x03 READ : 0x00 MISS : 0x00 type(0x01) */
   fdt_event_ctr_map[6] = cpu_to_be32(0x00010019);
   fdt_event_ctr_map[7] = cpu_to_be32(0x00010019);
   fdt_event_ctr_map[8] = cpu_to_be32(cmask);

   /* SBI_PMU_HW_CACHE_DTLB : 0x03 WRITE : 0x01 MISS : 0x00 type(0x01) */
   fdt_event_ctr_map[9] = cpu_to_be32(0x0001001B);
   fdt_event_ctr_map[10] = cpu_to_be32(0x0001001B);
   fdt_event_ctr_map[11] = cpu_to_be32(cmask);

   /* SBI_PMU_HW_CACHE_ITLB : 0x04 READ : 0x00 MISS : 0x00 type(0x01) */
   fdt_event_ctr_map[12] = cpu_to_be32(0x00010021);
   fdt_event_ctr_map[13] = cpu_to_be32(0x00010021);
   fdt_event_ctr_map[14] = cpu_to_be32(cmask);

   /* This a OpenSBI specific DT property documented in OpenSBI docs */
   qemu_fdt_setprop(fdt, pmu_name, "riscv,event-to-mhpmcounters",
                    fdt_event_ctr_map, sizeof(fdt_event_ctr_map));
}

static bool riscv_pmu_counter_valid(RISCVCPU *cpu, uint32_t ctr_idx)
{
    if (ctr_idx < 3 || ctr_idx >= RV_MAX_MHPMCOUNTERS ||
        !(cpu->pmu_avail_ctrs & BIT(ctr_idx))) {
        return false;
    } else {
        return true;
    }
}

static bool riscv_pmu_counter_enabled(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    if (riscv_pmu_counter_valid(cpu, ctr_idx) &&
        !get_field(env->mcountinhibit, BIT(ctr_idx))) {
        return true;
    } else {
        return false;
    }
}

static int riscv_pmu_incr_ctr_rv32(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;
    target_ulong max_val = UINT32_MAX;
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    bool virt_on = env->virt_enabled;

    /* Privilege mode filtering */
    if ((env->priv == PRV_M &&
        (env->mhpmeventh_val[ctr_idx] & MHPMEVENTH_BIT_MINH)) ||
        (env->priv == PRV_S && virt_on &&
        (env->mhpmeventh_val[ctr_idx] & MHPMEVENTH_BIT_VSINH)) ||
        (env->priv == PRV_U && virt_on &&
        (env->mhpmeventh_val[ctr_idx] & MHPMEVENTH_BIT_VUINH)) ||
        (env->priv == PRV_S && !virt_on &&
        (env->mhpmeventh_val[ctr_idx] & MHPMEVENTH_BIT_SINH)) ||
        (env->priv == PRV_U && !virt_on &&
        (env->mhpmeventh_val[ctr_idx] & MHPMEVENTH_BIT_UINH))) {
        return 0;
    }

    /* Handle the overflow scenario */
    if (counter->mhpmcounter_val == max_val) {
        if (counter->mhpmcounterh_val == max_val) {
            counter->mhpmcounter_val = 0;
            counter->mhpmcounterh_val = 0;
            /* Generate interrupt only if OF bit is clear */
            if (!(env->mhpmeventh_val[ctr_idx] & MHPMEVENTH_BIT_OF)) {
                env->mhpmeventh_val[ctr_idx] |= MHPMEVENTH_BIT_OF;
                riscv_cpu_update_mip(env, MIP_LCOFIP, BOOL_TO_MASK(1));
            }
        } else {
            counter->mhpmcounterh_val++;
        }
    } else {
        counter->mhpmcounter_val++;
    }

    return 0;
}

static int riscv_pmu_incr_ctr_rv64(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    uint64_t max_val = UINT64_MAX;
    bool virt_on = env->virt_enabled;

    /* Privilege mode filtering */
    if ((env->priv == PRV_M &&
        (env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_MINH)) ||
        (env->priv == PRV_S && virt_on &&
        (env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_VSINH)) ||
        (env->priv == PRV_U && virt_on &&
        (env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_VUINH)) ||
        (env->priv == PRV_S && !virt_on &&
        (env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_SINH)) ||
        (env->priv == PRV_U && !virt_on &&
        (env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_UINH))) {
        return 0;
    }

    /* Handle the overflow scenario */
    if (counter->mhpmcounter_val == max_val) {
        counter->mhpmcounter_val = 0;
        /* Generate interrupt only if OF bit is clear */
        if (!(env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_OF)) {
            env->mhpmevent_val[ctr_idx] |= MHPMEVENT_BIT_OF;
            riscv_cpu_update_mip(env, MIP_LCOFIP, BOOL_TO_MASK(1));
        }
    } else {
        counter->mhpmcounter_val++;
    }
    return 0;
}

/*
 * Information needed to update counters:
 *  new_priv, new_virt: To correctly save starting snapshot for the newly
 *                      started mode. Look at array being indexed with newprv.
 *  old_priv, old_virt: To correctly select previous snapshot for old priv
 *                      and compute delta. Also to select correct counter
 *                      to inc. Look at arrays being indexed with env->priv.
 *
 *  To avoid the complexity of calling this function, we assume that
 *  env->priv and env->virt_enabled contain old priv and old virt and
 *  new priv and new virt values are passed in as arguments.
 */
static void riscv_pmu_icount_update_priv(CPURISCVState *env,
                                         target_ulong newpriv, bool new_virt)
{
    uint64_t *snapshot_prev, *snapshot_new;
    uint64_t current_icount;
    uint64_t *counter_arr;
    uint64_t delta;

    if (icount_enabled()) {
        current_icount = icount_get_raw();
    } else {
        current_icount = cpu_get_host_ticks();
    }

    if (env->virt_enabled) {
        g_assert(env->priv <= PRV_S);
        counter_arr = env->pmu_fixed_ctrs[1].counter_virt;
        snapshot_prev = env->pmu_fixed_ctrs[1].counter_virt_prev;
    } else {
        counter_arr = env->pmu_fixed_ctrs[1].counter;
        snapshot_prev = env->pmu_fixed_ctrs[1].counter_prev;
    }

    if (new_virt) {
        g_assert(newpriv <= PRV_S);
        snapshot_new = env->pmu_fixed_ctrs[1].counter_virt_prev;
    } else {
        snapshot_new = env->pmu_fixed_ctrs[1].counter_prev;
    }

     /*
      * new_priv can be same as env->priv. So we need to calculate
      * delta first before updating snapshot_new[new_priv].
      */
    delta = current_icount - snapshot_prev[env->priv];
    snapshot_new[newpriv] = current_icount;

    counter_arr[env->priv] += delta;
}

static void riscv_pmu_cycle_update_priv(CPURISCVState *env,
                                        target_ulong newpriv, bool new_virt)
{
    uint64_t *snapshot_prev, *snapshot_new;
    uint64_t current_ticks;
    uint64_t *counter_arr;
    uint64_t delta;

    if (icount_enabled()) {
        current_ticks = icount_get();
    } else {
        current_ticks = cpu_get_host_ticks();
    }

    if (env->virt_enabled) {
        g_assert(env->priv <= PRV_S);
        counter_arr = env->pmu_fixed_ctrs[0].counter_virt;
        snapshot_prev = env->pmu_fixed_ctrs[0].counter_virt_prev;
    } else {
        counter_arr = env->pmu_fixed_ctrs[0].counter;
        snapshot_prev = env->pmu_fixed_ctrs[0].counter_prev;
    }

    if (new_virt) {
        g_assert(newpriv <= PRV_S);
        snapshot_new = env->pmu_fixed_ctrs[0].counter_virt_prev;
    } else {
        snapshot_new = env->pmu_fixed_ctrs[0].counter_prev;
    }

    delta = current_ticks - snapshot_prev[env->priv];
    snapshot_new[newpriv] = current_ticks;

    counter_arr[env->priv] += delta;
}

void riscv_pmu_update_fixed_ctrs(CPURISCVState *env, target_ulong newpriv,
                                 bool new_virt)
{
    riscv_pmu_cycle_update_priv(env, newpriv, new_virt);
    riscv_pmu_icount_update_priv(env, newpriv, new_virt);
}

int riscv_pmu_incr_ctr(RISCVCPU *cpu, enum riscv_pmu_event_idx event_idx)
{
    uint32_t ctr_idx;
    int ret;
    CPURISCVState *env = &cpu->env;
    gpointer value;

    if (!cpu->cfg.pmu_mask) {
        return 0;
    }
    value = g_hash_table_lookup(cpu->pmu_event_ctr_map,
                                GUINT_TO_POINTER(event_idx));
    if (!value) {
        return -1;
    }

    ctr_idx = GPOINTER_TO_UINT(value);
    if (!riscv_pmu_counter_enabled(cpu, ctr_idx)) {
        return -1;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        ret = riscv_pmu_incr_ctr_rv32(cpu, ctr_idx);
    } else {
        ret = riscv_pmu_incr_ctr_rv64(cpu, ctr_idx);
    }

    return ret;
}

bool riscv_pmu_ctr_monitor_instructions(CPURISCVState *env,
                                        uint32_t target_ctr)
{
    RISCVCPU *cpu;
    uint32_t event_idx;
    uint32_t ctr_idx;

    /* Fixed instret counter */
    if (target_ctr == 2) {
        return true;
    }

    cpu = env_archcpu(env);
    if (!cpu->pmu_event_ctr_map) {
        return false;
    }

    event_idx = RISCV_PMU_EVENT_HW_INSTRUCTIONS;
    ctr_idx = GPOINTER_TO_UINT(g_hash_table_lookup(cpu->pmu_event_ctr_map,
                               GUINT_TO_POINTER(event_idx)));
    if (!ctr_idx) {
        return false;
    }

    return target_ctr == ctr_idx ? true : false;
}

bool riscv_pmu_ctr_monitor_cycles(CPURISCVState *env, uint32_t target_ctr)
{
    RISCVCPU *cpu;
    uint32_t event_idx;
    uint32_t ctr_idx;

    /* Fixed mcycle counter */
    if (target_ctr == 0) {
        return true;
    }

    cpu = env_archcpu(env);
    if (!cpu->pmu_event_ctr_map) {
        return false;
    }

    event_idx = RISCV_PMU_EVENT_HW_CPU_CYCLES;
    ctr_idx = GPOINTER_TO_UINT(g_hash_table_lookup(cpu->pmu_event_ctr_map,
                               GUINT_TO_POINTER(event_idx)));

    /* Counter zero is not used for event_ctr_map */
    if (!ctr_idx) {
        return false;
    }

    return (target_ctr == ctr_idx) ? true : false;
}

static gboolean pmu_remove_event_map(gpointer key, gpointer value,
                                     gpointer udata)
{
    return (GPOINTER_TO_UINT(value) == GPOINTER_TO_UINT(udata)) ? true : false;
}

static int64_t pmu_icount_ticks_to_ns(int64_t value)
{
    int64_t ret = 0;

    if (icount_enabled()) {
        ret = icount_to_ns(value);
    } else {
        ret = (NANOSECONDS_PER_SECOND / RISCV_TIMEBASE_FREQ) * value;
    }

    return ret;
}

int riscv_pmu_update_event_map(CPURISCVState *env, uint64_t value,
                               uint32_t ctr_idx)
{
    uint32_t event_idx;
    RISCVCPU *cpu = env_archcpu(env);

    if (!riscv_pmu_counter_valid(cpu, ctr_idx) || !cpu->pmu_event_ctr_map) {
        return -1;
    }

    /*
     * Expected mhpmevent value is zero for reset case. Remove the current
     * mapping.
     */
    if (!(value & MHPMEVENT_IDX_MASK)) {
        g_hash_table_foreach_remove(cpu->pmu_event_ctr_map,
                                    pmu_remove_event_map,
                                    GUINT_TO_POINTER(ctr_idx));
        return 0;
    }

    event_idx = value & MHPMEVENT_IDX_MASK;
    if (g_hash_table_lookup(cpu->pmu_event_ctr_map,
                            GUINT_TO_POINTER(event_idx))) {
        return 0;
    }

    switch (event_idx) {
    case RISCV_PMU_EVENT_HW_CPU_CYCLES:
    case RISCV_PMU_EVENT_HW_INSTRUCTIONS:
    case RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS:
    case RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS:
    case RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS:
        break;
    default:
        /* We don't support any raw events right now */
        return -1;
    }
    g_hash_table_insert(cpu->pmu_event_ctr_map, GUINT_TO_POINTER(event_idx),
                        GUINT_TO_POINTER(ctr_idx));

    return 0;
}

static bool pmu_hpmevent_is_of_set(CPURISCVState *env, uint32_t ctr_idx)
{
    target_ulong mhpmevent_val;
    uint64_t of_bit_mask;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        mhpmevent_val = env->mhpmeventh_val[ctr_idx];
        of_bit_mask = MHPMEVENTH_BIT_OF;
     } else {
        mhpmevent_val = env->mhpmevent_val[ctr_idx];
        of_bit_mask = MHPMEVENT_BIT_OF;
    }

    return get_field(mhpmevent_val, of_bit_mask);
}

static bool pmu_hpmevent_set_of_if_clear(CPURISCVState *env, uint32_t ctr_idx)
{
    target_ulong *mhpmevent_val;
    uint64_t of_bit_mask;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        mhpmevent_val = &env->mhpmeventh_val[ctr_idx];
        of_bit_mask = MHPMEVENTH_BIT_OF;
     } else {
        mhpmevent_val = &env->mhpmevent_val[ctr_idx];
        of_bit_mask = MHPMEVENT_BIT_OF;
    }

    if (!get_field(*mhpmevent_val, of_bit_mask)) {
        *mhpmevent_val |= of_bit_mask;
        return true;
    }

    return false;
}

static void pmu_timer_trigger_irq(RISCVCPU *cpu,
                                  enum riscv_pmu_event_idx evt_idx)
{
    uint32_t ctr_idx;
    CPURISCVState *env = &cpu->env;
    PMUCTRState *counter;
    int64_t irq_trigger_at;
    uint64_t curr_ctr_val, curr_ctrh_val;
    uint64_t ctr_val;

    if (evt_idx != RISCV_PMU_EVENT_HW_CPU_CYCLES &&
        evt_idx != RISCV_PMU_EVENT_HW_INSTRUCTIONS) {
        return;
    }

    ctr_idx = GPOINTER_TO_UINT(g_hash_table_lookup(cpu->pmu_event_ctr_map,
                               GUINT_TO_POINTER(evt_idx)));
    if (!riscv_pmu_counter_enabled(cpu, ctr_idx)) {
        return;
    }

    /* Generate interrupt only if OF bit is clear */
    if (pmu_hpmevent_is_of_set(env, ctr_idx)) {
        return;
    }

    counter = &env->pmu_ctrs[ctr_idx];
    if (counter->irq_overflow_left > 0) {
        irq_trigger_at = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                        counter->irq_overflow_left;
        timer_mod_anticipate_ns(cpu->pmu_timer, irq_trigger_at);
        counter->irq_overflow_left = 0;
        return;
    }

    riscv_pmu_read_ctr(env, (target_ulong *)&curr_ctr_val, false, ctr_idx);
    ctr_val = counter->mhpmcounter_val;
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        riscv_pmu_read_ctr(env, (target_ulong *)&curr_ctrh_val, true, ctr_idx);
        curr_ctr_val = curr_ctr_val | (curr_ctrh_val << 32);
        ctr_val = ctr_val |
                ((uint64_t)counter->mhpmcounterh_val << 32);
    }

    /*
     * We can not accommodate for inhibited modes when setting up timer. Check
     * if the counter has actually overflowed or not by comparing current
     * counter value (accommodated for inhibited modes) with software written
     * counter value.
     */
    if (curr_ctr_val >= ctr_val) {
        riscv_pmu_setup_timer(env, curr_ctr_val, ctr_idx);
        return;
    }

    if (cpu->pmu_avail_ctrs & BIT(ctr_idx)) {
        if (pmu_hpmevent_set_of_if_clear(env, ctr_idx)) {
            riscv_cpu_update_mip(env, MIP_LCOFIP, BOOL_TO_MASK(1));
        }
    }
}

/* Timer callback for instret and cycle counter overflow */
void riscv_pmu_timer_cb(void *priv)
{
    RISCVCPU *cpu = priv;

    /* Timer event was triggered only for these events */
    pmu_timer_trigger_irq(cpu, RISCV_PMU_EVENT_HW_CPU_CYCLES);
    pmu_timer_trigger_irq(cpu, RISCV_PMU_EVENT_HW_INSTRUCTIONS);
}

int riscv_pmu_setup_timer(CPURISCVState *env, uint64_t value, uint32_t ctr_idx)
{
    uint64_t overflow_delta, overflow_at, curr_ns;
    int64_t overflow_ns, overflow_left = 0;
    RISCVCPU *cpu = env_archcpu(env);
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];

    /* No need to setup a timer if LCOFI is disabled when OF is set */
    if (!riscv_pmu_counter_valid(cpu, ctr_idx) || !cpu->cfg.ext_sscofpmf ||
        pmu_hpmevent_is_of_set(env, ctr_idx)) {
        return -1;
    }

    if (value) {
        overflow_delta = UINT64_MAX - value + 1;
    } else {
        overflow_delta = UINT64_MAX;
    }

    /*
     * QEMU supports only int64_t timers while RISC-V counters are uint64_t.
     * Compute the leftover and save it so that it can be reprogrammed again
     * when timer expires.
     */
    if (overflow_delta > INT64_MAX) {
        overflow_left = overflow_delta - INT64_MAX;
    }

    if (riscv_pmu_ctr_monitor_cycles(env, ctr_idx) ||
        riscv_pmu_ctr_monitor_instructions(env, ctr_idx)) {
        overflow_ns = pmu_icount_ticks_to_ns((int64_t)overflow_delta);
        overflow_left = pmu_icount_ticks_to_ns(overflow_left) ;
    } else {
        return -1;
    }
    curr_ns = (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    overflow_at =  curr_ns + overflow_ns;
    if (overflow_at <= curr_ns)
        overflow_at = UINT64_MAX;

    if (overflow_at > INT64_MAX) {
        overflow_left += overflow_at - INT64_MAX;
        counter->irq_overflow_left = overflow_left;
        overflow_at = INT64_MAX;
    }
    timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);

    return 0;
}


void riscv_pmu_init(RISCVCPU *cpu, Error **errp)
{
    if (cpu->cfg.pmu_mask & (COUNTEREN_CY | COUNTEREN_TM | COUNTEREN_IR)) {
        error_setg(errp, "\"pmu-mask\" contains invalid bits (0-2) set");
        return;
    }

    if (ctpop32(cpu->cfg.pmu_mask) > (RV_MAX_MHPMCOUNTERS - 3)) {
        error_setg(errp, "Number of counters exceeds maximum available");
        return;
    }

    cpu->pmu_event_ctr_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!cpu->pmu_event_ctr_map) {
        error_setg(errp, "Unable to allocate PMU event hash table");
        return;
    }

    cpu->pmu_avail_ctrs = cpu->cfg.pmu_mask;
}
