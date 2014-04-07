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
#include "config.h"
#include "qemu-common.h"
#include "exec/gdbstub.h"

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
        return 4;
    case 64:
        /* nip */
    case 65:
        /* msr */
    case 67:
        /* lr */
    case 68:
        /* ctr */
    case 69:
        /* xer */
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


/* Old gdb always expects FP registers.  Newer (xml-aware) gdb only
 * expects whatever the target description contains.  Due to a
 * historical mishap the FP registers appear in between core integer
 * regs and PC, MSR, CR, and so forth.  We hack round this by giving the
 * FP regs zero size when talking to a newer gdb.
 */

int ppc_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int r = ppc_gdb_register_len(n);

    if (!r) {
        return r;
    }

    if (n < 32) {
        /* gprs */
        gdb_get_regl(mem_buf, env->gpr[n]);
    } else if (n < 64) {
        /* fprs */
        stfq_p(mem_buf, env->fpr[n-32]);
    } else {
        switch (n) {
        case 64:
            gdb_get_regl(mem_buf, env->nip);
            break;
        case 65:
            gdb_get_regl(mem_buf, env->msr);
            break;
        case 66:
            {
                uint32_t cr = 0;
                int i;
                for (i = 0; i < 8; i++) {
                    cr |= env->crf[i] << (32 - ((i + 1) * 4));
                }
                gdb_get_reg32(mem_buf, cr);
                break;
            }
        case 67:
            gdb_get_regl(mem_buf, env->lr);
            break;
        case 68:
            gdb_get_regl(mem_buf, env->ctr);
            break;
        case 69:
            gdb_get_regl(mem_buf, env->xer);
            break;
        case 70:
            gdb_get_reg32(mem_buf, env->fpscr);
            break;
        }
    }
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
    if (n < 32) {
        /* gprs */
        env->gpr[n] = ldtul_p(mem_buf);
    } else if (n < 64) {
        /* fprs */
        env->fpr[n-32] = ldfq_p(mem_buf);
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
            env->xer = ldtul_p(mem_buf);
            break;
        case 70:
            /* fpscr */
            store_fpscr(env, ldtul_p(mem_buf), 0xffffffff);
            break;
        }
    }
    return r;
}
