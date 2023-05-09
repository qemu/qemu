/*
 * ARM gdb server stub: AArch64 specific functions.
 *
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
#include "qemu/log.h"
#include "cpu.h"
#include "internals.h"
#include "gdbstub/helpers.h"

int aarch64_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 31) {
        /* Core integer register.  */
        return gdb_get_reg64(mem_buf, env->xregs[n]);
    }
    switch (n) {
    case 31:
        return gdb_get_reg64(mem_buf, env->xregs[31]);
    case 32:
        return gdb_get_reg64(mem_buf, env->pc);
    case 33:
        return gdb_get_reg32(mem_buf, pstate_read(env));
    }
    /* Unknown register.  */
    return 0;
}

int aarch64_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tmp;

    tmp = ldq_p(mem_buf);

    if (n < 31) {
        /* Core integer register.  */
        env->xregs[n] = tmp;
        return 8;
    }
    switch (n) {
    case 31:
        env->xregs[31] = tmp;
        return 8;
    case 32:
        env->pc = tmp;
        return 8;
    case 33:
        /* CPSR */
        pstate_write(env, tmp);
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

int aarch64_gdb_get_fpu_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    switch (reg) {
    case 0 ... 31:
    {
        /* 128 bit FP register - quads are in LE order */
        uint64_t *q = aa64_vfp_qreg(env, reg);
        return gdb_get_reg128(buf, q[1], q[0]);
    }
    case 32:
        /* FPSR */
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        /* FPCR */
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    default:
        return 0;
    }
}

int aarch64_gdb_set_fpu_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    switch (reg) {
    case 0 ... 31:
        /* 128 bit FP register */
        {
            uint64_t *q = aa64_vfp_qreg(env, reg);
            q[0] = ldq_le_p(buf);
            q[1] = ldq_le_p(buf + 8);
            return 16;
        }
    case 32:
        /* FPSR */
        vfp_set_fpsr(env, ldl_p(buf));
        return 4;
    case 33:
        /* FPCR */
        vfp_set_fpcr(env, ldl_p(buf));
        return 4;
    default:
        return 0;
    }
}

int aarch64_gdb_get_sve_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);

    switch (reg) {
    /* The first 32 registers are the zregs */
    case 0 ... 31:
    {
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            len += gdb_get_reg128(buf,
                                  env->vfp.zregs[reg].d[vq * 2 + 1],
                                  env->vfp.zregs[reg].d[vq * 2]);
        }
        return len;
    }
    case 32:
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    /* then 16 predicates and the ffr */
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            len += gdb_get_reg64(buf, env->vfp.pregs[preg].p[vq / 4]);
        }
        return len;
    }
    case 51:
    {
        /*
         * We report in Vector Granules (VG) which is 64bit in a Z reg
         * while the ZCR works in Vector Quads (VQ) which is 128bit chunks.
         */
        int vq = sve_vqm1_for_el(env, arm_current_el(env)) + 1;
        return gdb_get_reg64(buf, vq * 2);
    }
    default:
        /* gdbstub asked for something out our range */
        qemu_log_mask(LOG_UNIMP, "%s: out of range register %d", __func__, reg);
        break;
    }

    return 0;
}

int aarch64_gdb_set_sve_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);

    /* The first 32 registers are the zregs */
    switch (reg) {
    /* The first 32 registers are the zregs */
    case 0 ... 31:
    {
        int vq, len = 0;
        uint64_t *p = (uint64_t *) buf;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            env->vfp.zregs[reg].d[vq * 2 + 1] = *p++;
            env->vfp.zregs[reg].d[vq * 2] = *p++;
            len += 16;
        }
        return len;
    }
    case 32:
        vfp_set_fpsr(env, *(uint32_t *)buf);
        return 4;
    case 33:
        vfp_set_fpcr(env, *(uint32_t *)buf);
        return 4;
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        uint64_t *p = (uint64_t *) buf;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            env->vfp.pregs[preg].p[vq / 4] = *p++;
            len += 8;
        }
        return len;
    }
    case 51:
        /* cannot set vg via gdbstub */
        return 0;
    default:
        /* gdbstub asked for something out our range */
        break;
    }

    return 0;
}

int aarch64_gdb_get_pauth_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    switch (reg) {
    case 0: /* pauth_dmask */
    case 1: /* pauth_cmask */
    case 2: /* pauth_dmask_high */
    case 3: /* pauth_cmask_high */
        /*
         * Note that older versions of this feature only contained
         * pauth_{d,c}mask, for use with Linux user processes, and
         * thus exclusively in the low half of the address space.
         *
         * To support system mode, and to debug kernels, two new regs
         * were added to cover the high half of the address space.
         * For the purpose of pauth_ptr_mask, we can use any well-formed
         * address within the address space half -- here, 0 and -1.
         */
        {
            bool is_data = !(reg & 1);
            bool is_high = reg & 2;
            ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
            ARMVAParameters param;

            param = aa64_va_parameters(env, -is_high, mmu_idx, is_data, false);
            return gdb_get_reg64(buf, pauth_ptr_mask(param));
        }
    default:
        return 0;
    }
}

int aarch64_gdb_set_pauth_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    /* All pseudo registers are read-only. */
    return 0;
}

static void output_vector_union_type(GString *s, int reg_width,
                                     const char *name)
{
    struct TypeSize {
        const char *gdb_type;
        short size;
        char sz, suffix;
    };

    static const struct TypeSize vec_lanes[] = {
        /* quads */
        { "uint128", 128, 'q', 'u' },
        { "int128", 128, 'q', 's' },
        /* 64 bit */
        { "ieee_double", 64, 'd', 'f' },
        { "uint64", 64, 'd', 'u' },
        { "int64", 64, 'd', 's' },
        /* 32 bit */
        { "ieee_single", 32, 's', 'f' },
        { "uint32", 32, 's', 'u' },
        { "int32", 32, 's', 's' },
        /* 16 bit */
        { "ieee_half", 16, 'h', 'f' },
        { "uint16", 16, 'h', 'u' },
        { "int16", 16, 'h', 's' },
        /* bytes */
        { "uint8", 8, 'b', 'u' },
        { "int8", 8, 'b', 's' },
    };

    static const char suf[] = { 'b', 'h', 's', 'd', 'q' };
    int i, j;

    /* First define types and totals in a whole VL */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        g_string_append_printf(s,
                               "<vector id=\"%s%c%c\" type=\"%s\" count=\"%d\"/>",
                               name, vec_lanes[i].sz, vec_lanes[i].suffix,
                               vec_lanes[i].gdb_type, reg_width / vec_lanes[i].size);
    }

    /*
     * Now define a union for each size group containing unsigned and
     * signed and potentially float versions of each size from 128 to
     * 8 bits.
     */
    for (i = 0; i < ARRAY_SIZE(suf); i++) {
        int bits = 8 << i;

        g_string_append_printf(s, "<union id=\"%sn%c\">", name, suf[i]);
        for (j = 0; j < ARRAY_SIZE(vec_lanes); j++) {
            if (vec_lanes[j].size == bits) {
                g_string_append_printf(s, "<field name=\"%c\" type=\"%s%c%c\"/>",
                                       vec_lanes[j].suffix, name,
                                       vec_lanes[j].sz, vec_lanes[j].suffix);
            }
        }
        g_string_append(s, "</union>");
    }

    /* And now the final union of unions */
    g_string_append_printf(s, "<union id=\"%s\">", name);
    for (i = ARRAY_SIZE(suf) - 1; i >= 0; i--) {
        g_string_append_printf(s, "<field name=\"%c\" type=\"%sn%c\"/>",
                               suf[i], name, suf[i]);
    }
    g_string_append(s, "</union>");
}

int arm_gen_dynamic_svereg_xml(CPUState *cs, int orig_base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    DynamicGDBXMLInfo *info = &cpu->dyn_svereg_xml;
    int reg_width = cpu->sve_max_vq * 128;
    int pred_width = cpu->sve_max_vq * 16;
    int base_reg = orig_base_reg;
    int i;

    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.gnu.gdb.aarch64.sve\">");

    /* Create the vector union type. */
    output_vector_union_type(s, reg_width, "svev");

    /* Create the predicate vector type. */
    g_string_append_printf(s,
                           "<vector id=\"svep\" type=\"uint8\" count=\"%d\"/>",
                           pred_width / 8);

    /* Define the vector registers. */
    for (i = 0; i < 32; i++) {
        g_string_append_printf(s,
                               "<reg name=\"z%d\" bitsize=\"%d\""
                               " regnum=\"%d\" type=\"svev\"/>",
                               i, reg_width, base_reg++);
    }

    /* fpscr & status registers */
    g_string_append_printf(s, "<reg name=\"fpsr\" bitsize=\"32\""
                           " regnum=\"%d\" group=\"float\""
                           " type=\"int\"/>", base_reg++);
    g_string_append_printf(s, "<reg name=\"fpcr\" bitsize=\"32\""
                           " regnum=\"%d\" group=\"float\""
                           " type=\"int\"/>", base_reg++);

    /* Define the predicate registers. */
    for (i = 0; i < 16; i++) {
        g_string_append_printf(s,
                               "<reg name=\"p%d\" bitsize=\"%d\""
                               " regnum=\"%d\" type=\"svep\"/>",
                               i, pred_width, base_reg++);
    }
    g_string_append_printf(s,
                           "<reg name=\"ffr\" bitsize=\"%d\""
                           " regnum=\"%d\" group=\"vector\""
                           " type=\"svep\"/>",
                           pred_width, base_reg++);

    /* Define the vector length pseudo-register. */
    g_string_append_printf(s,
                           "<reg name=\"vg\" bitsize=\"64\""
                           " regnum=\"%d\" type=\"int\"/>",
                           base_reg++);

    g_string_append_printf(s, "</feature>");

    info->desc = g_string_free(s, false);
    info->num = base_reg - orig_base_reg;
    return info->num;
}
