/*
 * RISC-V GDB Server Stub
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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
#include "exec/gdbstub.h"
#include "cpu.h"

struct TypeSize {
    const char *gdb_type;
    const char *id;
    int size;
    const char suffix;
};

static const struct TypeSize vec_lanes[] = {
    /* quads */
    { "uint128", "quads", 128, 'q' },
    /* 64 bit */
    { "uint64", "longs", 64, 'l' },
    /* 32 bit */
    { "uint32", "words", 32, 'w' },
    /* 16 bit */
    { "uint16", "shorts", 16, 's' },
    /*
     * TODO: currently there is no reliable way of telling
     * if the remote gdb actually understands ieee_half so
     * we don't expose it in the target description for now.
     * { "ieee_half", 16, 'h', 'f' },
     */
    /* bytes */
    { "uint8", "bytes", 8, 'b' },
};

int riscv_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong tmp;

    if (n < 32) {
        tmp = env->gpr[n];
    } else if (n == 32) {
        tmp = env->pc;
    } else {
        return 0;
    }

    switch (env->misa_mxl_max) {
    case MXL_RV32:
        return gdb_get_reg32(mem_buf, tmp);
    case MXL_RV64:
    case MXL_RV128:
        return gdb_get_reg64(mem_buf, tmp);
    default:
        g_assert_not_reached();
    }
    return 0;
}

int riscv_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int length = 0;
    target_ulong tmp;

    switch (env->misa_mxl_max) {
    case MXL_RV32:
        tmp = (int32_t)ldl_p(mem_buf);
        length = 4;
        break;
    case MXL_RV64:
    case MXL_RV128:
        if (env->xl < MXL_RV64) {
            tmp = (int32_t)ldq_p(mem_buf);
        } else {
            tmp = ldq_p(mem_buf);
        }
        length = 8;
        break;
    default:
        g_assert_not_reached();
    }
    if (n > 0 && n < 32) {
        env->gpr[n] = tmp;
    } else if (n == 32) {
        env->pc = tmp;
    }

    return length;
}

static int riscv_gdb_get_fpu(CPURISCVState *env, GByteArray *buf, int n)
{
    if (n < 32) {
        if (env->misa_ext & RVD) {
            return gdb_get_reg64(buf, env->fpr[n]);
        }
        if (env->misa_ext & RVF) {
            return gdb_get_reg32(buf, env->fpr[n]);
        }
    }
    return 0;
}

static int riscv_gdb_set_fpu(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        env->fpr[n] = ldq_p(mem_buf); /* always 64-bit */
        return sizeof(uint64_t);
    }
    return 0;
}

/*
 * Convert register index number passed by GDB to the correspond
 * vector CSR number. Vector CSRs are defined after vector registers
 * in dynamic generated riscv-vector.xml, thus the starting register index
 * of vector CSRs is 32.
 * Return 0 if register index number is out of range.
 */
static int riscv_gdb_vector_csrno(int num_regs)
{
    /*
     * The order of vector CSRs in the switch case
     * should match with the order defined in csr_ops[].
     */
    switch (num_regs) {
    case 32:
        return CSR_VSTART;
    case 33:
        return CSR_VXSAT;
    case 34:
        return CSR_VXRM;
    case 35:
        return CSR_VCSR;
    case 36:
        return CSR_VL;
    case 37:
        return CSR_VTYPE;
    case 38:
        return CSR_VLENB;
    default:
        /* Unknown register. */
        return 0;
    }
}

static int riscv_gdb_get_vector(CPURISCVState *env, GByteArray *buf, int n)
{
    uint16_t vlenb = env_archcpu(env)->cfg.vlen >> 3;
    if (n < 32) {
        int i;
        int cnt = 0;
        for (i = 0; i < vlenb; i += 8) {
            cnt += gdb_get_reg64(buf,
                                 env->vreg[(n * vlenb + i) / 8]);
        }
        return cnt;
    }

    int csrno = riscv_gdb_vector_csrno(n);

    if (!csrno) {
        return 0;
    }

    target_ulong val = 0;
    int result = riscv_csrrw_debug(env, csrno, &val, 0, 0);

    if (result == RISCV_EXCP_NONE) {
        return gdb_get_regl(buf, val);
    }

    return 0;
}

static int riscv_gdb_set_vector(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    uint16_t vlenb = env_archcpu(env)->cfg.vlen >> 3;
    if (n < 32) {
        int i;
        for (i = 0; i < vlenb; i += 8) {
            env->vreg[(n * vlenb + i) / 8] = ldq_p(mem_buf + i);
        }
        return vlenb;
    }

    int csrno = riscv_gdb_vector_csrno(n);

    if (!csrno) {
        return 0;
    }

    target_ulong val = ldtul_p(mem_buf);
    int result = riscv_csrrw_debug(env, csrno, NULL, val, -1);

    if (result == RISCV_EXCP_NONE) {
        return sizeof(target_ulong);
    }

    return 0;
}

static int riscv_gdb_get_csr(CPURISCVState *env, GByteArray *buf, int n)
{
    if (n < CSR_TABLE_SIZE) {
        target_ulong val = 0;
        int result;

        result = riscv_csrrw_debug(env, n, &val, 0, 0);
        if (result == RISCV_EXCP_NONE) {
            return gdb_get_regl(buf, val);
        }
    }
    return 0;
}

static int riscv_gdb_set_csr(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < CSR_TABLE_SIZE) {
        target_ulong val = ldtul_p(mem_buf);
        int result;

        result = riscv_csrrw_debug(env, n, NULL, val, -1);
        if (result == RISCV_EXCP_NONE) {
            return sizeof(target_ulong);
        }
    }
    return 0;
}

static int riscv_gdb_get_virtual(CPURISCVState *cs, GByteArray *buf, int n)
{
    if (n == 0) {
#ifdef CONFIG_USER_ONLY
        return gdb_get_regl(buf, 0);
#else
        return gdb_get_regl(buf, cs->priv);
#endif
    }
    return 0;
}

static int riscv_gdb_set_virtual(CPURISCVState *cs, uint8_t *mem_buf, int n)
{
    if (n == 0) {
#ifndef CONFIG_USER_ONLY
        cs->priv = ldtul_p(mem_buf) & 0x3;
        if (cs->priv == PRV_H) {
            cs->priv = PRV_S;
        }
#endif
        return sizeof(target_ulong);
    }
    return 0;
}

static int riscv_gen_dynamic_csr_xml(CPUState *cs, int base_reg)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    GString *s = g_string_new(NULL);
    riscv_csr_predicate_fn predicate;
    int bitsize = 16 << env->misa_mxl_max;
    int i;

    /* Until gdb knows about 128-bit registers */
    if (bitsize > 64) {
        bitsize = 64;
    }

    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.gnu.gdb.riscv.csr\">");

    for (i = 0; i < CSR_TABLE_SIZE; i++) {
        predicate = csr_ops[i].predicate;
        if (predicate && (predicate(env, i) == RISCV_EXCP_NONE)) {
            if (csr_ops[i].name) {
                g_string_append_printf(s, "<reg name=\"%s\"", csr_ops[i].name);
            } else {
                g_string_append_printf(s, "<reg name=\"csr%03x\"", i);
            }
            g_string_append_printf(s, " bitsize=\"%d\"", bitsize);
            g_string_append_printf(s, " regnum=\"%d\"/>", base_reg + i);
        }
    }

    g_string_append_printf(s, "</feature>");

    cpu->dyn_csr_xml = g_string_free(s, false);
    return CSR_TABLE_SIZE;
}

static int ricsv_gen_dynamic_vector_xml(CPUState *cs, int base_reg)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    GString *s = g_string_new(NULL);
    g_autoptr(GString) ts = g_string_new("");
    int reg_width = cpu->cfg.vlen;
    int num_regs = 0;
    int i;

    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.gnu.gdb.riscv.vector\">");

    /* First define types and totals in a whole VL */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        int count = reg_width / vec_lanes[i].size;
        g_string_printf(ts, "%s", vec_lanes[i].id);
        g_string_append_printf(s,
                               "<vector id=\"%s\" type=\"%s\" count=\"%d\"/>",
                               ts->str, vec_lanes[i].gdb_type, count);
    }

    /* Define unions */
    g_string_append_printf(s, "<union id=\"riscv_vector\">");
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        g_string_append_printf(s, "<field name=\"%c\" type=\"%s\"/>",
                               vec_lanes[i].suffix,
                               vec_lanes[i].id);
    }
    g_string_append(s, "</union>");

    /* Define vector registers */
    for (i = 0; i < 32; i++) {
        g_string_append_printf(s,
                               "<reg name=\"v%d\" bitsize=\"%d\""
                               " regnum=\"%d\" group=\"vector\""
                               " type=\"riscv_vector\"/>",
                               i, reg_width, base_reg++);
        num_regs++;
    }

    /* Define vector CSRs */
    const char *vector_csrs[7] = {
        "vstart", "vxsat", "vxrm", "vcsr",
        "vl", "vtype", "vlenb"
    };

    for (i = 0; i < 7; i++) {
        g_string_append_printf(s,
                               "<reg name=\"%s\" bitsize=\"%d\""
                               " regnum=\"%d\" group=\"vector\""
                               " type=\"int\"/>",
                               vector_csrs[i], TARGET_LONG_BITS, base_reg++);
        num_regs++;
    }

    g_string_append_printf(s, "</feature>");

    cpu->dyn_vreg_xml = g_string_free(s, false);
    return num_regs;
}

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (env->misa_ext & RVD) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 32, "riscv-64bit-fpu.xml", 0);
    } else if (env->misa_ext & RVF) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 32, "riscv-32bit-fpu.xml", 0);
    }
    if (env->misa_ext & RVV) {
        gdb_register_coprocessor(cs, riscv_gdb_get_vector, riscv_gdb_set_vector,
                                 ricsv_gen_dynamic_vector_xml(cs,
                                                              cs->gdb_num_regs),
                                 "riscv-vector.xml", 0);
    }
    switch (env->misa_mxl_max) {
    case MXL_RV32:
        gdb_register_coprocessor(cs, riscv_gdb_get_virtual,
                                 riscv_gdb_set_virtual,
                                 1, "riscv-32bit-virtual.xml", 0);
        break;
    case MXL_RV64:
    case MXL_RV128:
        gdb_register_coprocessor(cs, riscv_gdb_get_virtual,
                                 riscv_gdb_set_virtual,
                                 1, "riscv-64bit-virtual.xml", 0);
        break;
    default:
        g_assert_not_reached();
    }

    gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                             riscv_gen_dynamic_csr_xml(cs, cs->gdb_num_regs),
                             "riscv-csr.xml", 0);
}
