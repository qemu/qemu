/*
 * SPARC gdb server stub
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

#ifdef TARGET_ABI32
#define GET_REGA(val) GET_REG32(val)
#else
#define GET_REGA(val) GET_REGL(val)
#endif

static int cpu_gdb_read_register(CPUSPARCState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        /* g0..g7 */
        GET_REGA(env->gregs[n]);
    }
    if (n < 32) {
        /* register window */
        GET_REGA(env->regwptr[n - 8]);
    }
#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    if (n < 64) {
        /* fprs */
        if (n & 1) {
            GET_REG32(env->fpr[(n - 32) / 2].l.lower);
        } else {
            GET_REG32(env->fpr[(n - 32) / 2].l.upper);
        }
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    switch (n) {
    case 64:
        GET_REGA(env->y);
    case 65:
        GET_REGA(cpu_get_psr(env));
    case 66:
        GET_REGA(env->wim);
    case 67:
        GET_REGA(env->tbr);
    case 68:
        GET_REGA(env->pc);
    case 69:
        GET_REGA(env->npc);
    case 70:
        GET_REGA(env->fsr);
    case 71:
        GET_REGA(0); /* csr */
    default:
        GET_REGA(0);
    }
#else
    if (n < 64) {
        /* f0-f31 */
        if (n & 1) {
            GET_REG32(env->fpr[(n - 32) / 2].l.lower);
        } else {
            GET_REG32(env->fpr[(n - 32) / 2].l.upper);
        }
    }
    if (n < 80) {
        /* f32-f62 (double width, even numbers only) */
        GET_REG64(env->fpr[(n - 32) / 2].ll);
    }
    switch (n) {
    case 80:
        GET_REGL(env->pc);
    case 81:
        GET_REGL(env->npc);
    case 82:
        GET_REGL((cpu_get_ccr(env) << 32) |
                 ((env->asi & 0xff) << 24) |
                 ((env->pstate & 0xfff) << 8) |
                 cpu_get_cwp64(env));
    case 83:
        GET_REGL(env->fsr);
    case 84:
        GET_REGL(env->fprs);
    case 85:
        GET_REGL(env->y);
    }
#endif
    return 0;
}

static int cpu_gdb_write_register(CPUSPARCState *env, uint8_t *mem_buf, int n)
{
#if defined(TARGET_ABI32)
    abi_ulong tmp;

    tmp = ldl_p(mem_buf);
#else
    target_ulong tmp;

    tmp = ldtul_p(mem_buf);
#endif

    if (n < 8) {
        /* g0..g7 */
        env->gregs[n] = tmp;
    } else if (n < 32) {
        /* register window */
        env->regwptr[n - 8] = tmp;
    }
#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    else if (n < 64) {
        /* fprs */
        /* f0-f31 */
        if (n & 1) {
            env->fpr[(n - 32) / 2].l.lower = tmp;
        } else {
            env->fpr[(n - 32) / 2].l.upper = tmp;
        }
    } else {
        /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
        switch (n) {
        case 64:
            env->y = tmp;
            break;
        case 65:
            cpu_put_psr(env, tmp);
            break;
        case 66:
            env->wim = tmp;
            break;
        case 67:
            env->tbr = tmp;
            break;
        case 68:
            env->pc = tmp;
            break;
        case 69:
            env->npc = tmp;
            break;
        case 70:
            env->fsr = tmp;
            break;
        default:
            return 0;
        }
    }
    return 4;
#else
    else if (n < 64) {
        /* f0-f31 */
        tmp = ldl_p(mem_buf);
        if (n & 1) {
            env->fpr[(n - 32) / 2].l.lower = tmp;
        } else {
            env->fpr[(n - 32) / 2].l.upper = tmp;
        }
        return 4;
    } else if (n < 80) {
        /* f32-f62 (double width, even numbers only) */
        env->fpr[(n - 32) / 2].ll = tmp;
    } else {
        switch (n) {
        case 80:
            env->pc = tmp;
            break;
        case 81:
            env->npc = tmp;
            break;
        case 82:
            cpu_put_ccr(env, tmp >> 32);
            env->asi = (tmp >> 24) & 0xff;
            env->pstate = (tmp >> 8) & 0xfff;
            cpu_put_cwp64(env, tmp & 0xff);
            break;
        case 83:
            env->fsr = tmp;
            break;
        case 84:
            env->fprs = tmp;
            break;
        case 85:
            env->y = tmp;
            break;
        default:
            return 0;
        }
    }
    return 8;
#endif
}
