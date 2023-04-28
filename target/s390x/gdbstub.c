/*
 * s390x gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#include "qemu/bitops.h"
#include "sysemu/hw_accel.h"
#include "sysemu/tcg.h"

int s390_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;

    switch (n) {
    case S390_PSWM_REGNUM:
        return gdb_get_regl(mem_buf, s390_cpu_get_psw_mask(env));
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
        s390_cpu_set_psw(env, tmpl, env->psw.addr);
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

static int cpu_read_ac_reg(CPUS390XState *env, GByteArray *buf, int n)
{
    switch (n) {
    case S390_A0_REGNUM ... S390_A15_REGNUM:
        return gdb_get_reg32(buf, env->aregs[n]);
    default:
        return 0;
    }
}

static int cpu_write_ac_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_A0_REGNUM ... S390_A15_REGNUM:
        env->aregs[n] = ldl_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
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

static int cpu_read_fp_reg(CPUS390XState *env, GByteArray *buf, int n)
{
    switch (n) {
    case S390_FPC_REGNUM:
        return gdb_get_reg32(buf, env->fpc);
    case S390_F0_REGNUM ... S390_F15_REGNUM:
        return gdb_get_reg64(buf, *get_freg(env, n - S390_F0_REGNUM));
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
        *get_freg(env, n - S390_F0_REGNUM) = ldtul_p(mem_buf);
        return 8;
    default:
        return 0;
    }
}

/* the values represent the positions in s390-vx.xml */
#define S390_V0L_REGNUM 0
#define S390_V15L_REGNUM 15
#define S390_V16_REGNUM 16
#define S390_V31_REGNUM 31
/* total number of registers in s390-vx.xml */
#define S390_NUM_VREGS 32

static int cpu_read_vreg(CPUS390XState *env, GByteArray *buf, int n)
{
    int ret;

    switch (n) {
    case S390_V0L_REGNUM ... S390_V15L_REGNUM:
        ret = gdb_get_reg64(buf, env->vregs[n][1]);
        break;
    case S390_V16_REGNUM ... S390_V31_REGNUM:
        ret = gdb_get_reg64(buf, env->vregs[n][0]);
        ret += gdb_get_reg64(buf, env->vregs[n][1]);
        break;
    default:
        ret = 0;
    }

    return ret;
}

static int cpu_write_vreg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_V0L_REGNUM ... S390_V15L_REGNUM:
        env->vregs[n][1] = ldtul_p(mem_buf + 8);
        return 8;
    case S390_V16_REGNUM ... S390_V31_REGNUM:
        env->vregs[n][0] = ldtul_p(mem_buf);
        env->vregs[n][1] = ldtul_p(mem_buf + 8);
        return 16;
    default:
        return 0;
    }
}

/* the values represent the positions in s390-cr.xml */
#define S390_C0_REGNUM 0
#define S390_C15_REGNUM 15
/* total number of registers in s390-cr.xml */
#define S390_NUM_C_REGS 16

#ifndef CONFIG_USER_ONLY
static int cpu_read_c_reg(CPUS390XState *env, GByteArray *buf, int n)
{
    switch (n) {
    case S390_C0_REGNUM ... S390_C15_REGNUM:
        return gdb_get_regl(buf, env->cregs[n]);
    default:
        return 0;
    }
}

static int cpu_write_c_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_C0_REGNUM ... S390_C15_REGNUM:
        env->cregs[n] = ldtul_p(mem_buf);
        if (tcg_enabled()) {
            tlb_flush(env_cpu(env));
        }
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    default:
        return 0;
    }
}

/* the values represent the positions in s390-virt.xml */
#define S390_VIRT_CKC_REGNUM    0
#define S390_VIRT_CPUTM_REGNUM  1
#define S390_VIRT_BEA_REGNUM    2
#define S390_VIRT_PREFIX_REGNUM 3
/* total number of registers in s390-virt.xml */
#define S390_NUM_VIRT_REGS 4

static int cpu_read_virt_reg(CPUS390XState *env, GByteArray *mem_buf, int n)
{
    switch (n) {
    case S390_VIRT_CKC_REGNUM:
        return gdb_get_regl(mem_buf, env->ckc);
    case S390_VIRT_CPUTM_REGNUM:
        return gdb_get_regl(mem_buf, env->cputm);
    case S390_VIRT_BEA_REGNUM:
        return gdb_get_regl(mem_buf, env->gbea);
    case S390_VIRT_PREFIX_REGNUM:
        return gdb_get_regl(mem_buf, env->psa);
    default:
        return 0;
    }
}

static int cpu_write_virt_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_VIRT_CKC_REGNUM:
        env->ckc = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    case S390_VIRT_CPUTM_REGNUM:
        env->cputm = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    case S390_VIRT_BEA_REGNUM:
        env->gbea = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    case S390_VIRT_PREFIX_REGNUM:
        env->psa = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    default:
        return 0;
    }
}

/* the values represent the positions in s390-virt-kvm.xml */
#define S390_VIRT_KVM_PP_REGNUM     0
#define S390_VIRT_KVM_PFT_REGNUM    1
#define S390_VIRT_KVM_PFS_REGNUM    2
#define S390_VIRT_KVM_PFC_REGNUM    3
/* total number of registers in s390-virt-kvm.xml */
#define S390_NUM_VIRT_KVM_REGS 4

static int cpu_read_virt_kvm_reg(CPUS390XState *env, GByteArray *mem_buf, int n)
{
    switch (n) {
    case S390_VIRT_KVM_PP_REGNUM:
        return gdb_get_regl(mem_buf, env->pp);
    case S390_VIRT_KVM_PFT_REGNUM:
        return gdb_get_regl(mem_buf, env->pfault_token);
    case S390_VIRT_KVM_PFS_REGNUM:
        return gdb_get_regl(mem_buf, env->pfault_select);
    case S390_VIRT_KVM_PFC_REGNUM:
        return gdb_get_regl(mem_buf, env->pfault_compare);
    default:
        return 0;
    }
}

static int cpu_write_virt_kvm_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case S390_VIRT_KVM_PP_REGNUM:
        env->pp = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    case S390_VIRT_KVM_PFT_REGNUM:
        env->pfault_token = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    case S390_VIRT_KVM_PFS_REGNUM:
        env->pfault_select = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    case S390_VIRT_KVM_PFC_REGNUM:
        env->pfault_compare = ldtul_p(mem_buf);
        cpu_synchronize_post_init(env_cpu(env));
        return 8;
    default:
        return 0;
    }
}
#endif

/* the values represent the positions in s390-gs.xml */
#define S390_GS_RESERVED_REGNUM 0
#define S390_GS_GSD_REGNUM      1
#define S390_GS_GSSM_REGNUM     2
#define S390_GS_GSEPLA_REGNUM   3
/* total number of registers in s390-gs.xml */
#define S390_NUM_GS_REGS 4

static int cpu_read_gs_reg(CPUS390XState *env, GByteArray *buf, int n)
{
    return gdb_get_regl(buf, env->gscb[n]);
}

static int cpu_write_gs_reg(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    env->gscb[n] = ldtul_p(mem_buf);
    cpu_synchronize_post_init(env_cpu(env));
    return 8;
}

void s390_cpu_gdb_init(CPUState *cs)
{
    gdb_register_coprocessor(cs, cpu_read_ac_reg,
                             cpu_write_ac_reg,
                             S390_NUM_AC_REGS, "s390-acr.xml", 0);

    gdb_register_coprocessor(cs, cpu_read_fp_reg,
                             cpu_write_fp_reg,
                             S390_NUM_FP_REGS, "s390-fpr.xml", 0);

    gdb_register_coprocessor(cs, cpu_read_vreg,
                             cpu_write_vreg,
                             S390_NUM_VREGS, "s390-vx.xml", 0);

    gdb_register_coprocessor(cs, cpu_read_gs_reg,
                             cpu_write_gs_reg,
                             S390_NUM_GS_REGS, "s390-gs.xml", 0);

#ifndef CONFIG_USER_ONLY
    gdb_register_coprocessor(cs, cpu_read_c_reg,
                             cpu_write_c_reg,
                             S390_NUM_C_REGS, "s390-cr.xml", 0);

    gdb_register_coprocessor(cs, cpu_read_virt_reg,
                             cpu_write_virt_reg,
                             S390_NUM_VIRT_REGS, "s390-virt.xml", 0);

    if (kvm_enabled()) {
        gdb_register_coprocessor(cs, cpu_read_virt_kvm_reg,
                                 cpu_write_virt_kvm_reg,
                                 S390_NUM_VIRT_KVM_REGS, "s390-virt-kvm.xml",
                                 0);
    }
#endif
}
