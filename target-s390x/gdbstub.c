/*
 * s390x gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#include "qemu-common.h"
#include "exec/gdbstub.h"
#include "qemu/bitops.h"

int s390_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    uint64_t val;
    int cc_op;

    switch (n) {
    case S390_PSWM_REGNUM:
        if (tcg_enabled()) {
            cc_op = calc_cc(env, env->cc_op, env->cc_src, env->cc_dst,
                            env->cc_vr);
            val = deposit64(env->psw.mask, 44, 2, cc_op);
            return gdb_get_regl(mem_buf, val);
        }
        return gdb_get_regl(mem_buf, env->psw.mask);
    case S390_PSWA_REGNUM:
        return gdb_get_regl(mem_buf, env->psw.addr);
    case S390_R0_REGNUM ... S390_R15_REGNUM:
        return gdb_get_regl(mem_buf, env->regs[n - S390_R0_REGNUM]);
    }
    return 0;
}

int s390_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    target_ulong tmpl = ldtul_p(mem_buf);

    switch (n) {
    case S390_PSWM_REGNUM:
        env->psw.mask = tmpl;
        if (tcg_enabled()) {
            env->cc_op = extract64(tmpl, 44, 2);
        }
        break;
    case S390_PSWA_REGNUM:
        env->psw.addr = tmpl;
        break;
    case S390_R0_REGNUM ... S390_R15_REGNUM:
        env->regs[n - S390_R0_REGNUM] = tmpl;
        break;
    default:
        return 0;
    }
    return 8;
}

/* the values represent the positions in s390-acr.xml */
#define S390_A0_REGNUM 0
#define S390_A15_REGNUM 15
/* total number of registers in s390-acr.xml */
#define S390_NUM_AC_REGS 16

static int cpu_read_ac_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_A0_REGNUM ... S390_A15_REGNUM:
        return gdb_get_reg32(mem_buf, env->aregs[n]);
    default:
        return 0;
    }
}

static int cpu_write_ac_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_A0_REGNUM ... S390_A15_REGNUM:
        env->aregs[n] = ldl_p(mem_buf);
        return 4;
    default:
        return 0;
    }
}

/* the values represent the positions in s390-fpr.xml */
#define S390_FPC_REGNUM 0
#define S390_F0_REGNUM 1
#define S390_F15_REGNUM 16
/* total number of registers in s390-fpr.xml */
#define S390_NUM_FP_REGS 17

static int cpu_read_fp_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_FPC_REGNUM:
        return gdb_get_reg32(mem_buf, env->fpc);
    case S390_F0_REGNUM ... S390_F15_REGNUM:
        return gdb_get_reg64(mem_buf, env->fregs[n - S390_F0_REGNUM].ll);
    default:
        return 0;
    }
}

static int cpu_write_fp_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_FPC_REGNUM:
        env->fpc = ldl_p(mem_buf);
        return 4;
    case S390_F0_REGNUM ... S390_F15_REGNUM:
        env->fregs[n - S390_F0_REGNUM].ll = ldtul_p(mem_buf);
        return 8;
    default:
        return 0;
    }
}

void s390_cpu_gdb_init(CPUState *cs)
{
    gdb_register_coprocessor(cs, cpu_read_ac_reg,
                             cpu_write_ac_reg,
                             S390_NUM_AC_REGS, "s390-acr.xml", 0);

    gdb_register_coprocessor(cs, cpu_read_fp_reg,
                             cpu_write_fp_reg,
                             S390_NUM_FP_REGS, "s390-fpr.xml", 0);
}
