/*
 * PowerPC gdb server stub
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
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"

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
 * TARGET_WORDS_BIGENDIAN is set to the proper ordering for the
 * binary, and cannot be changed.  For system mode,
 * TARGET_WORDS_BIGENDIAN is always set, and we must check the current
 * mode of the chip to see if we're running in little-endian.
 */
void ppc_maybe_bswap_register(CPUPPCState *env, uint8_t *mem_buf, int len)
{
#ifndef CONFIG_USER_ONLY
    if (!msr_le) {
        /* do nothing */
    } else if (len == 4) {
        bswap32s((uint32_t *)mem_buf);
    } else if (len == 8) {
        bswap64s((uint64_t *)mem_buf);
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
                uint32_t cr = 0;
                int i;
                for (i = 0; i < 8; i++) {
                    cr |= env->crf[i] << (32 - ((i + 1) * 4));
                }
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
            gdb_get_reg32(buf, env->xer);
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
                uint32_t cr = 0;
                int i;
                for (i = 0; i < 8; i++) {
                    cr |= env->crf[i] << (32 - ((i + 1) * 4));
                }
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
            gdb_get_reg32(buf, env->xer);
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
        *cpu_fpr_ptr(env, n - 32) = ldfq_p(mem_buf);
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
                int i;
                for (i = 0; i < 8; i++) {
                    env->crf[i] = (cr >> (32 - ((i + 1) * 4))) & 0xF;
                }
                break;
            }
        case 67:
            env->lr = ldtul_p(mem_buf);
            break;
        case 68:
            env->ctr = ldtul_p(mem_buf);
            break;
        case 69:
            env->xer = ldl_p(mem_buf);
            break;
        case 70:
            /* fpscr */
            store_fpscr(env, ldtul_p(mem_buf), 0xffffffff);
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
        *cpu_fpr_ptr(env, n - 32) = ldfq_p(mem_buf);
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
                int i;
                for (i = 0; i < 8; i++) {
                    env->crf[i] = (cr >> (32 - ((i + 1) * 4))) & 0xF;
                }
                break;
            }
        case 67 + 32:
            env->lr = ldq_p(mem_buf);
            break;
        case 68 + 32:
            env->ctr = ldq_p(mem_buf);
            break;
        case 69 + 32:
            env->xer = ldl_p(mem_buf);
            break;
        case 70 + 32:
            /* fpscr */
            store_fpscr(env, ldq_p(mem_buf), 0xffffffff);
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
