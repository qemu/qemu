/*
 * QEMU RISC-V Native Debug Support
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This provides the native debug support via the Trigger Module, as defined
 * in the RISC-V Debug Specification:
 * https://github.com/riscv/riscv-debug-spec/raw/master/riscv-debug-stable.pdf
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
#include "qapi/error.h"
#include "cpu.h"
#include "trace.h"
#include "exec/exec-all.h"

/*
 * The following M-mode trigger CSRs are implemented:
 *
 * - tselect
 * - tdata1
 * - tdata2
 * - tdata3
 *
 * We don't support writable 'type' field in the tdata1 register, so there is
 * no need to implement the "tinfo" CSR.
 *
 * The following triggers are implemented:
 *
 * Index | Type |          tdata mapping | Description
 * ------+------+------------------------+------------
 *     0 |    2 |         tdata1, tdata2 | Address / Data Match
 *     1 |    2 |         tdata1, tdata2 | Address / Data Match
 */

/* tdata availability of a trigger */
typedef bool tdata_avail[TDATA_NUM];

static tdata_avail tdata_mapping[TRIGGER_NUM] = {
    [TRIGGER_TYPE2_IDX_0 ... TRIGGER_TYPE2_IDX_1] = { true, true, false },
};

/* only breakpoint size 1/2/4/8 supported */
static int access_size[SIZE_NUM] = {
    [SIZE_ANY] = 0,
    [SIZE_1B]  = 1,
    [SIZE_2B]  = 2,
    [SIZE_4B]  = 4,
    [SIZE_6B]  = -1,
    [SIZE_8B]  = 8,
    [6 ... 15] = -1,
};

static inline target_ulong trigger_type(CPURISCVState *env,
                                        trigger_type_t type)
{
    target_ulong tdata1;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        tdata1 = RV32_TYPE(type);
        break;
    case MXL_RV64:
        tdata1 = RV64_TYPE(type);
        break;
    default:
        g_assert_not_reached();
    }

    return tdata1;
}

bool tdata_available(CPURISCVState *env, int tdata_index)
{
    if (unlikely(tdata_index >= TDATA_NUM)) {
        return false;
    }

    if (unlikely(env->trigger_cur >= TRIGGER_NUM)) {
        return false;
    }

    return tdata_mapping[env->trigger_cur][tdata_index];
}

target_ulong tselect_csr_read(CPURISCVState *env)
{
    return env->trigger_cur;
}

void tselect_csr_write(CPURISCVState *env, target_ulong val)
{
    /* all target_ulong bits of tselect are implemented */
    env->trigger_cur = val;
}

static target_ulong tdata1_validate(CPURISCVState *env, target_ulong val,
                                    trigger_type_t t)
{
    uint32_t type, dmode;
    target_ulong tdata1;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        type = extract32(val, 28, 4);
        dmode = extract32(val, 27, 1);
        tdata1 = RV32_TYPE(t);
        break;
    case MXL_RV64:
        type = extract64(val, 60, 4);
        dmode = extract64(val, 59, 1);
        tdata1 = RV64_TYPE(t);
        break;
    default:
        g_assert_not_reached();
    }

    if (type != t) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring type write to tdata1 register\n");
    }
    if (dmode != 0) {
        qemu_log_mask(LOG_UNIMP, "debug mode is not supported\n");
    }

    return tdata1;
}

static inline void warn_always_zero_bit(target_ulong val, target_ulong mask,
                                        const char *msg)
{
    if (val & mask) {
        qemu_log_mask(LOG_UNIMP, "%s bit is always zero\n", msg);
    }
}

static uint32_t type2_breakpoint_size(CPURISCVState *env, target_ulong ctrl)
{
    uint32_t size, sizelo, sizehi = 0;

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        sizehi = extract32(ctrl, 21, 2);
    }
    sizelo = extract32(ctrl, 16, 2);
    size = (sizehi << 2) | sizelo;

    return size;
}

static inline bool type2_breakpoint_enabled(target_ulong ctrl)
{
    bool mode = !!(ctrl & (TYPE2_U | TYPE2_S | TYPE2_M));
    bool rwx = !!(ctrl & (TYPE2_LOAD | TYPE2_STORE | TYPE2_EXEC));

    return mode && rwx;
}

static target_ulong type2_mcontrol_validate(CPURISCVState *env,
                                            target_ulong ctrl)
{
    target_ulong val;
    uint32_t size;

    /* validate the generic part first */
    val = tdata1_validate(env, ctrl, TRIGGER_TYPE_AD_MATCH);

    /* validate unimplemented (always zero) bits */
    warn_always_zero_bit(ctrl, TYPE2_MATCH, "match");
    warn_always_zero_bit(ctrl, TYPE2_CHAIN, "chain");
    warn_always_zero_bit(ctrl, TYPE2_ACTION, "action");
    warn_always_zero_bit(ctrl, TYPE2_TIMING, "timing");
    warn_always_zero_bit(ctrl, TYPE2_SELECT, "select");
    warn_always_zero_bit(ctrl, TYPE2_HIT, "hit");

    /* validate size encoding */
    size = type2_breakpoint_size(env, ctrl);
    if (access_size[size] == -1) {
        qemu_log_mask(LOG_UNIMP, "access size %d is not supported, using SIZE_ANY\n",
                      size);
    } else {
        val |= (ctrl & TYPE2_SIZELO);
        if (riscv_cpu_mxl(env) == MXL_RV64) {
            val |= (ctrl & TYPE2_SIZEHI);
        }
    }

    /* keep the mode and attribute bits */
    val |= (ctrl & (TYPE2_U | TYPE2_S | TYPE2_M |
                    TYPE2_LOAD | TYPE2_STORE | TYPE2_EXEC));

    return val;
}

static void type2_breakpoint_insert(CPURISCVState *env, target_ulong index)
{
    target_ulong ctrl = env->type2_trig[index].mcontrol;
    target_ulong addr = env->type2_trig[index].maddress;
    bool enabled = type2_breakpoint_enabled(ctrl);
    CPUState *cs = env_cpu(env);
    int flags = BP_CPU | BP_STOP_BEFORE_ACCESS;
    uint32_t size;

    if (!enabled) {
        return;
    }

    if (ctrl & TYPE2_EXEC) {
        cpu_breakpoint_insert(cs, addr, flags, &env->type2_trig[index].bp);
    }

    if (ctrl & TYPE2_LOAD) {
        flags |= BP_MEM_READ;
    }
    if (ctrl & TYPE2_STORE) {
        flags |= BP_MEM_WRITE;
    }

    if (flags & BP_MEM_ACCESS) {
        size = type2_breakpoint_size(env, ctrl);
        if (size != 0) {
            cpu_watchpoint_insert(cs, addr, size, flags,
                                  &env->type2_trig[index].wp);
        } else {
            cpu_watchpoint_insert(cs, addr, 8, flags,
                                  &env->type2_trig[index].wp);
        }
    }
}

static void type2_breakpoint_remove(CPURISCVState *env, target_ulong index)
{
    CPUState *cs = env_cpu(env);

    if (env->type2_trig[index].bp) {
        cpu_breakpoint_remove_by_ref(cs, env->type2_trig[index].bp);
        env->type2_trig[index].bp = NULL;
    }

    if (env->type2_trig[index].wp) {
        cpu_watchpoint_remove_by_ref(cs, env->type2_trig[index].wp);
        env->type2_trig[index].wp = NULL;
    }
}

static target_ulong type2_reg_read(CPURISCVState *env,
                                   target_ulong trigger_index, int tdata_index)
{
    uint32_t index = trigger_index - TRIGGER_TYPE2_IDX_0;
    target_ulong tdata;

    switch (tdata_index) {
    case TDATA1:
        tdata = env->type2_trig[index].mcontrol;
        break;
    case TDATA2:
        tdata = env->type2_trig[index].maddress;
        break;
    default:
        g_assert_not_reached();
    }

    return tdata;
}

static void type2_reg_write(CPURISCVState *env, target_ulong trigger_index,
                            int tdata_index, target_ulong val)
{
    uint32_t index = trigger_index - TRIGGER_TYPE2_IDX_0;
    target_ulong new_val;

    switch (tdata_index) {
    case TDATA1:
        new_val = type2_mcontrol_validate(env, val);
        if (new_val != env->type2_trig[index].mcontrol) {
            env->type2_trig[index].mcontrol = new_val;
            type2_breakpoint_remove(env, index);
            type2_breakpoint_insert(env, index);
        }
        break;
    case TDATA2:
        if (val != env->type2_trig[index].maddress) {
            env->type2_trig[index].maddress = val;
            type2_breakpoint_remove(env, index);
            type2_breakpoint_insert(env, index);
        }
        break;
    default:
        g_assert_not_reached();
    }

    return;
}

typedef target_ulong (*tdata_read_func)(CPURISCVState *env,
                                        target_ulong trigger_index,
                                        int tdata_index);

static tdata_read_func trigger_read_funcs[TRIGGER_NUM] = {
    [TRIGGER_TYPE2_IDX_0 ... TRIGGER_TYPE2_IDX_1] = type2_reg_read,
};

typedef void (*tdata_write_func)(CPURISCVState *env,
                                 target_ulong trigger_index,
                                 int tdata_index,
                                 target_ulong val);

static tdata_write_func trigger_write_funcs[TRIGGER_NUM] = {
    [TRIGGER_TYPE2_IDX_0 ... TRIGGER_TYPE2_IDX_1] = type2_reg_write,
};

target_ulong tdata_csr_read(CPURISCVState *env, int tdata_index)
{
    tdata_read_func read_func = trigger_read_funcs[env->trigger_cur];

    return read_func(env, env->trigger_cur, tdata_index);
}

void tdata_csr_write(CPURISCVState *env, int tdata_index, target_ulong val)
{
    tdata_write_func write_func = trigger_write_funcs[env->trigger_cur];

    return write_func(env, env->trigger_cur, tdata_index, val);
}

void riscv_cpu_debug_excp_handler(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            cs->watchpoint_hit = NULL;
            riscv_raise_exception(env, RISCV_EXCP_BREAKPOINT, 0);
        }
    } else {
        if (cpu_breakpoint_test(cs, env->pc, BP_CPU)) {
            riscv_raise_exception(env, RISCV_EXCP_BREAKPOINT, 0);
        }
    }
}

bool riscv_cpu_debug_check_breakpoint(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    CPUBreakpoint *bp;
    target_ulong ctrl;
    target_ulong pc;
    int i;

    QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
        for (i = 0; i < TRIGGER_TYPE2_NUM; i++) {
            ctrl = env->type2_trig[i].mcontrol;
            pc = env->type2_trig[i].maddress;

            if ((ctrl & TYPE2_EXEC) && (bp->pc == pc)) {
                /* check U/S/M bit against current privilege level */
                if ((ctrl >> 3) & BIT(env->priv)) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool riscv_cpu_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong ctrl;
    target_ulong addr;
    int flags;
    int i;

    for (i = 0; i < TRIGGER_TYPE2_NUM; i++) {
        ctrl = env->type2_trig[i].mcontrol;
        addr = env->type2_trig[i].maddress;
        flags = 0;

        if (ctrl & TYPE2_LOAD) {
            flags |= BP_MEM_READ;
        }
        if (ctrl & TYPE2_STORE) {
            flags |= BP_MEM_WRITE;
        }

        if ((wp->flags & flags) && (wp->vaddr == addr)) {
            /* check U/S/M bit against current privilege level */
            if ((ctrl >> 3) & BIT(env->priv)) {
                return true;
            }
        }
    }

    return false;
}

void riscv_trigger_init(CPURISCVState *env)
{
    target_ulong type2 = trigger_type(env, TRIGGER_TYPE_AD_MATCH);
    int i;

    /* type 2 triggers */
    for (i = 0; i < TRIGGER_TYPE2_NUM; i++) {
        /*
         * type = TRIGGER_TYPE_AD_MATCH
         * dmode = 0 (both debug and M-mode can write tdata)
         * maskmax = 0 (unimplemented, always 0)
         * sizehi = 0 (match against any size, RV64 only)
         * hit = 0 (unimplemented, always 0)
         * select = 0 (always 0, perform match on address)
         * timing = 0 (always 0, trigger before instruction)
         * sizelo = 0 (match against any size)
         * action = 0 (always 0, raise a breakpoint exception)
         * chain = 0 (unimplemented, always 0)
         * match = 0 (always 0, when any compare value equals tdata2)
         */
        env->type2_trig[i].mcontrol = type2;
        env->type2_trig[i].maddress = 0;
        env->type2_trig[i].bp = NULL;
        env->type2_trig[i].wp = NULL;
    }
}
