/*
 * PowerPC gdb server stub
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
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#include "internal.h"

static int ppc_gdb_register_len_apple(int n)
{
    switch (n) {
    case 0 ... 31:
        /* gprs */
        return 8;
    case 32 ... 63:
        /* fprs */
        return 8;
    case 64 ... 95:
        return 16;
    case 64 + 32: /* nip */
    case 65 + 32: /* msr */
    case 67 + 32: /* lr */
    case 68 + 32: /* ctr */
    case 70 + 32: /* fpscr */
        return 8;
    case 66 + 32: /* cr */
    case 69 + 32: /* xer */
        return 4;
    default:
        return 0;
    }
}

static int ppc_gdb_register_len(int n)
{
    switch (n) {
    case 0 ... 31:
        /* gprs */
        return sizeof(target_ulong);
    case 32 ... 63:
        /* fprs */
        if (gdb_has_xml) {
            return 0;
        }
        return 8;
    case 66:
        /* cr */
    case 69:
        /* xer */
        return 4;
    case 64:
        /* nip */
    case 65:
        /* msr */
    case 67:
        /* lr */
    case 68:
        /* ctr */
        return sizeof(target_ulong);
    case 70:
        /* fpscr */
        if (gdb_has_xml) {
            return 0;
        }
        return sizeof(target_ulong);
    default:
        return 0;
    }
}

/*
 * We need to present the registers to gdb in the "current" memory
 * ordering.  For user-only mode we get this for free;
 * TARGET_BIG_ENDIAN is set to the proper ordering for the
 * binary, and cannot be changed.  For system mode,
 * TARGET_BIG_ENDIAN is always set, and we must check the current
 * mode of the chip to see if we're running in little-endian.
 */
void ppc_maybe_bswap_register(CPUPPCState *env, uint8_t *mem_buf, int len)
{
#ifndef CONFIG_USER_ONLY
    if (!FIELD_EX64(env->msr, MSR, LE)) {
        /* do nothing */
    } else if (len == 4) {
        bswap32s((uint32_t *)mem_buf);
    } else if (len == 8) {
        bswap64s((uint64_t *)mem_buf);
    } else if (len == 16) {
        bswap128s((Int128 *)mem_buf);
    } else {
        g_assert_not_reached();
    }
#endif
}

/*
 * Old gdb always expects FP registers.  Newer (xml-aware) gdb only
 * expects whatever the target description contains.  Due to a
 * historical mishap the FP registers appear in between core integer
 * regs and PC, MSR, CR, and so forth.  We hack round this by giving
 * the FP regs zero size when talking to a newer gdb.
 */

int ppc_cpu_gdb_read_register(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    uint8_t *mem_buf;
    int r = ppc_gdb_register_len(n);

    if (!r) {
        return r;
    }

    if (n < 32) {
        /* gprs */
        gdb_get_regl(buf, env->gpr[n]);
    } else if (n < 64) {
        /* fprs */
        gdb_get_reg64(buf, *cpu_fpr_ptr(env, n - 32));
    } else {
        switch (n) {
        case 64:
            gdb_get_regl(buf, env->nip);
            break;
        case 65:
            gdb_get_regl(buf, env->msr);
            break;
        case 66:
            {
                uint32_t cr = ppc_get_cr(env);
                gdb_get_reg32(buf, cr);
                break;
            }
        case 67:
            gdb_get_regl(buf, env->lr);
            break;
        case 68:
            gdb_get_regl(buf, env->ctr);
            break;
        case 69:
            gdb_get_reg32(buf, cpu_read_xer(env));
            break;
        case 70:
            gdb_get_reg32(buf, env->fpscr);
            break;
        }
    }
    mem_buf = buf->data + buf->len - r;
    ppc_maybe_bswap_register(env, mem_buf, r);
    return r;
}

int ppc_cpu_gdb_read_register_apple(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    uint8_t *mem_buf;
    int r = ppc_gdb_register_len_apple(n);

    if (!r) {
        return r;
    }

    if (n < 32) {
        /* gprs */
        gdb_get_reg64(buf, env->gpr[n]);
    } else if (n < 64) {
        /* fprs */
        gdb_get_reg64(buf, *cpu_fpr_ptr(env, n - 32));
    } else if (n < 96) {
        /* Altivec */
        gdb_get_reg64(buf, n - 64);
        gdb_get_reg64(buf, 0);
    } else {
        switch (n) {
        case 64 + 32:
            gdb_get_reg64(buf, env->nip);
            break;
        case 65 + 32:
            gdb_get_reg64(buf, env->msr);
            break;
        case 66 + 32:
            {
                uint32_t cr = ppc_get_cr(env);
                gdb_get_reg32(buf, cr);
                break;
            }
        case 67 + 32:
            gdb_get_reg64(buf, env->lr);
            break;
        case 68 + 32:
            gdb_get_reg64(buf, env->ctr);
            break;
        case 69 + 32:
            gdb_get_reg32(buf, cpu_read_xer(env));
            break;
        case 70 + 32:
            gdb_get_reg64(buf, env->fpscr);
            break;
        }
    }
    mem_buf = buf->data + buf->len - r;
    ppc_maybe_bswap_register(env, mem_buf, r);
    return r;
}

int ppc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int r = ppc_gdb_register_len(n);

    if (!r) {
        return r;
    }
    ppc_maybe_bswap_register(env, mem_buf, r);
    if (n < 32) {
        /* gprs */
        env->gpr[n] = ldtul_p(mem_buf);
    } else if (n < 64) {
        /* fprs */
        *cpu_fpr_ptr(env, n - 32) = ldq_p(mem_buf);
    } else {
        switch (n) {
        case 64:
            env->nip = ldtul_p(mem_buf);
            break;
        case 65:
            ppc_store_msr(env, ldtul_p(mem_buf));
            break;
        case 66:
            {
                uint32_t cr = ldl_p(mem_buf);
                ppc_set_cr(env, cr);
                break;
            }
        case 67:
            env->lr = ldtul_p(mem_buf);
            break;
        case 68:
            env->ctr = ldtul_p(mem_buf);
            break;
        case 69:
            cpu_write_xer(env, ldl_p(mem_buf));
            break;
        case 70:
            /* fpscr */
            ppc_store_fpscr(env, ldtul_p(mem_buf));
            break;
        }
    }
    return r;
}
int ppc_cpu_gdb_write_register_apple(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int r = ppc_gdb_register_len_apple(n);

    if (!r) {
        return r;
    }
    ppc_maybe_bswap_register(env, mem_buf, r);
    if (n < 32) {
        /* gprs */
        env->gpr[n] = ldq_p(mem_buf);
    } else if (n < 64) {
        /* fprs */
        *cpu_fpr_ptr(env, n - 32) = ldq_p(mem_buf);
    } else {
        switch (n) {
        case 64 + 32:
            env->nip = ldq_p(mem_buf);
            break;
        case 65 + 32:
            ppc_store_msr(env, ldq_p(mem_buf));
            break;
        case 66 + 32:
            {
                uint32_t cr = ldl_p(mem_buf);
                ppc_set_cr(env, cr);
                break;
            }
        case 67 + 32:
            env->lr = ldq_p(mem_buf);
            break;
        case 68 + 32:
            env->ctr = ldq_p(mem_buf);
            break;
        case 69 + 32:
            cpu_write_xer(env, ldl_p(mem_buf));
            break;
        case 70 + 32:
            /* fpscr */
            ppc_store_fpscr(env, ldq_p(mem_buf));
            break;
        }
    }
    return r;
}

#ifndef CONFIG_USER_ONLY
void ppc_gdb_gen_spr_xml(PowerPCCPU *cpu)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    GString *xml;
    char *spr_name;
    unsigned int num_regs = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (!spr->name) {
            continue;
        }

        /*
         * GDB identifies registers based on the order they are
         * presented in the XML. These ids will not match QEMU's
         * representation (which follows the PowerISA).
         *
         * Store the position of the current register description so
         * we can make the correspondence later.
         */
        spr->gdb_id = num_regs;
        num_regs++;
    }

    if (pcc->gdb_spr_xml) {
        return;
    }

    xml = g_string_new("<?xml version=\"1.0\"?>");
    g_string_append(xml, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append(xml, "<feature name=\"org.qemu.power.spr\">");

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (!spr->name) {
            continue;
        }

        spr_name = g_ascii_strdown(spr->name, -1);
        g_string_append_printf(xml, "<reg name=\"%s\"", spr_name);
        g_free(spr_name);

        g_string_append_printf(xml, " bitsize=\"%d\"", TARGET_LONG_BITS);
        g_string_append(xml, " group=\"spr\"/>");
    }

    g_string_append(xml, "</feature>");

    pcc->gdb_num_sprs = num_regs;
    pcc->gdb_spr_xml = g_string_free(xml, false);
}

const char *ppc_gdb_get_dynamic_xml(CPUState *cs, const char *xml_name)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);

    if (strcmp(xml_name, "power-spr.xml") == 0) {
        return pcc->gdb_spr_xml;
    }
    return NULL;
}
#endif

#if !defined(CONFIG_USER_ONLY)
static int gdb_find_spr_idx(CPUPPCState *env, int n)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (spr->name && spr->gdb_id == n) {
            return i;
        }
    }
    return -1;
}

static int gdb_get_spr_reg(CPUPPCState *env, GByteArray *buf, int n)
{
    int reg;
    int len;

    reg = gdb_find_spr_idx(env, n);
    if (reg < 0) {
        return 0;
    }

    len = TARGET_LONG_SIZE;
    gdb_get_regl(buf, env->spr[reg]);
    ppc_maybe_bswap_register(env, gdb_get_reg_ptr(buf, len), len);
    return len;
}

static int gdb_set_spr_reg(CPUPPCState *env, uint8_t *mem_buf, int n)
{
    int reg;
    int len;

    reg = gdb_find_spr_idx(env, n);
    if (reg < 0) {
        return 0;
    }

    len = TARGET_LONG_SIZE;
    ppc_maybe_bswap_register(env, mem_buf, len);
    env->spr[reg] = ldn_p(mem_buf, len);

    return len;
}
#endif

static int gdb_get_float_reg(CPUPPCState *env, GByteArray *buf, int n)
{
    uint8_t *mem_buf;
    if (n < 32) {
        gdb_get_reg64(buf, *cpu_fpr_ptr(env, n));
        mem_buf = gdb_get_reg_ptr(buf, 8);
        ppc_maybe_bswap_register(env, mem_buf, 8);
        return 8;
    }
    if (n == 32) {
        gdb_get_reg32(buf, env->fpscr);
        mem_buf = gdb_get_reg_ptr(buf, 4);
        ppc_maybe_bswap_register(env, mem_buf, 4);
        return 4;
    }
    return 0;
}

static int gdb_set_float_reg(CPUPPCState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        ppc_maybe_bswap_register(env, mem_buf, 8);
        *cpu_fpr_ptr(env, n) = ldq_p(mem_buf);
        return 8;
    }
    if (n == 32) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        ppc_store_fpscr(env, ldl_p(mem_buf));
        return 4;
    }
    return 0;
}

static int gdb_get_avr_reg(CPUPPCState *env, GByteArray *buf, int n)
{
    uint8_t *mem_buf;

    if (n < 32) {
        ppc_avr_t *avr = cpu_avr_ptr(env, n);
        gdb_get_reg128(buf, avr->VsrD(0), avr->VsrD(1));
        mem_buf = gdb_get_reg_ptr(buf, 16);
        ppc_maybe_bswap_register(env, mem_buf, 16);
        return 16;
    }
    if (n == 32) {
        gdb_get_reg32(buf, ppc_get_vscr(env));
        mem_buf = gdb_get_reg_ptr(buf, 4);
        ppc_maybe_bswap_register(env, mem_buf, 4);
        return 4;
    }
    if (n == 33) {
        gdb_get_reg32(buf, (uint32_t)env->spr[SPR_VRSAVE]);
        mem_buf = gdb_get_reg_ptr(buf, 4);
        ppc_maybe_bswap_register(env, mem_buf, 4);
        return 4;
    }
    return 0;
}

static int gdb_set_avr_reg(CPUPPCState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        ppc_avr_t *avr = cpu_avr_ptr(env, n);
        ppc_maybe_bswap_register(env, mem_buf, 16);
        avr->VsrD(0) = ldq_p(mem_buf);
        avr->VsrD(1) = ldq_p(mem_buf + 8);
        return 16;
    }
    if (n == 32) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        ppc_store_vscr(env, ldl_p(mem_buf));
        return 4;
    }
    if (n == 33) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        env->spr[SPR_VRSAVE] = (target_ulong)ldl_p(mem_buf);
        return 4;
    }
    return 0;
}

static int gdb_get_spe_reg(CPUPPCState *env, GByteArray *buf, int n)
{
    if (n < 32) {
#if defined(TARGET_PPC64)
        gdb_get_reg32(buf, env->gpr[n] >> 32);
        ppc_maybe_bswap_register(env, gdb_get_reg_ptr(buf, 4), 4);
#else
        gdb_get_reg32(buf, env->gprh[n]);
#endif
        return 4;
    }
    if (n == 32) {
        gdb_get_reg64(buf, env->spe_acc);
        ppc_maybe_bswap_register(env, gdb_get_reg_ptr(buf, 8), 8);
        return 8;
    }
    if (n == 33) {
        gdb_get_reg32(buf, env->spe_fscr);
        ppc_maybe_bswap_register(env, gdb_get_reg_ptr(buf, 4), 4);
        return 4;
    }
    return 0;
}

static int gdb_set_spe_reg(CPUPPCState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
#if defined(TARGET_PPC64)
        target_ulong lo = (uint32_t)env->gpr[n];
        target_ulong hi;

        ppc_maybe_bswap_register(env, mem_buf, 4);

        hi = (target_ulong)ldl_p(mem_buf) << 32;
        env->gpr[n] = lo | hi;
#else
        env->gprh[n] = ldl_p(mem_buf);
#endif
        return 4;
    }
    if (n == 32) {
        ppc_maybe_bswap_register(env, mem_buf, 8);
        env->spe_acc = ldq_p(mem_buf);
        return 8;
    }
    if (n == 33) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        env->spe_fscr = ldl_p(mem_buf);
        return 4;
    }
    return 0;
}

static int gdb_get_vsx_reg(CPUPPCState *env, GByteArray *buf, int n)
{
    if (n < 32) {
        gdb_get_reg64(buf, *cpu_vsrl_ptr(env, n));
        ppc_maybe_bswap_register(env, gdb_get_reg_ptr(buf, 8), 8);
        return 8;
    }
    return 0;
}

static int gdb_set_vsx_reg(CPUPPCState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        ppc_maybe_bswap_register(env, mem_buf, 8);
        *cpu_vsrl_ptr(env, n) = ldq_p(mem_buf);
        return 8;
    }
    return 0;
}

gchar *ppc_gdb_arch_name(CPUState *cs)
{
#if defined(TARGET_PPC64)
    return g_strdup("powerpc:common64");
#else
    return g_strdup("powerpc:common");
#endif
}

void ppc_gdb_init(CPUState *cs, PowerPCCPUClass *pcc)
{
    if (pcc->insns_flags & PPC_FLOAT) {
        gdb_register_coprocessor(cs, gdb_get_float_reg, gdb_set_float_reg,
                                 33, "power-fpu.xml", 0);
    }
    if (pcc->insns_flags & PPC_ALTIVEC) {
        gdb_register_coprocessor(cs, gdb_get_avr_reg, gdb_set_avr_reg,
                                 34, "power-altivec.xml", 0);
    }
    if (pcc->insns_flags & PPC_SPE) {
        gdb_register_coprocessor(cs, gdb_get_spe_reg, gdb_set_spe_reg,
                                 34, "power-spe.xml", 0);
    }
    if (pcc->insns_flags2 & PPC2_VSX) {
        gdb_register_coprocessor(cs, gdb_get_vsx_reg, gdb_set_vsx_reg,
                                 32, "power-vsx.xml", 0);
    }
#ifndef CONFIG_USER_ONLY
    gdb_register_coprocessor(cs, gdb_get_spr_reg, gdb_set_spr_reg,
                             pcc->gdb_num_sprs, "power-spr.xml", 0);
#endif
}
