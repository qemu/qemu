/*
 * SuperH gdb server stub
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

/* Hint: Use "set architecture sh4" in GDB to see fpu registers */
/* FIXME: We should use XML for this.  */

static int cpu_gdb_read_register(CPUSH4State *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case 0 ... 7:
        if ((env->sr & (SR_MD | SR_RB)) == (SR_MD | SR_RB)) {
            GET_REGL(env->gregs[n + 16]);
        } else {
            GET_REGL(env->gregs[n]);
        }
    case 8 ... 15:
        GET_REGL(env->gregs[n]);
    case 16:
        GET_REGL(env->pc);
    case 17:
        GET_REGL(env->pr);
    case 18:
        GET_REGL(env->gbr);
    case 19:
        GET_REGL(env->vbr);
    case 20:
        GET_REGL(env->mach);
    case 21:
        GET_REGL(env->macl);
    case 22:
        GET_REGL(env->sr);
    case 23:
        GET_REGL(env->fpul);
    case 24:
        GET_REGL(env->fpscr);
    case 25 ... 40:
        if (env->fpscr & FPSCR_FR) {
            stfl_p(mem_buf, env->fregs[n - 9]);
        } else {
            stfl_p(mem_buf, env->fregs[n - 25]);
        }
        return 4;
    case 41:
        GET_REGL(env->ssr);
    case 42:
        GET_REGL(env->spc);
    case 43 ... 50:
        GET_REGL(env->gregs[n - 43]);
    case 51 ... 58:
        GET_REGL(env->gregs[n - (51 - 16)]);
    }

    return 0;
}

static int cpu_gdb_write_register(CPUSH4State *env, uint8_t *mem_buf, int n)
{
    switch (n) {
    case 0 ... 7:
        if ((env->sr & (SR_MD | SR_RB)) == (SR_MD | SR_RB)) {
            env->gregs[n + 16] = ldl_p(mem_buf);
        } else {
            env->gregs[n] = ldl_p(mem_buf);
        }
        break;
    case 8 ... 15:
        env->gregs[n] = ldl_p(mem_buf);
        break;
    case 16:
        env->pc = ldl_p(mem_buf);
        break;
    case 17:
        env->pr = ldl_p(mem_buf);
        break;
    case 18:
        env->gbr = ldl_p(mem_buf);
        break;
    case 19:
        env->vbr = ldl_p(mem_buf);
        break;
    case 20:
        env->mach = ldl_p(mem_buf);
        break;
    case 21:
        env->macl = ldl_p(mem_buf);
        break;
    case 22:
        env->sr = ldl_p(mem_buf);
        break;
    case 23:
        env->fpul = ldl_p(mem_buf);
        break;
    case 24:
        env->fpscr = ldl_p(mem_buf);
        break;
    case 25 ... 40:
        if (env->fpscr & FPSCR_FR) {
            env->fregs[n - 9] = ldfl_p(mem_buf);
        } else {
            env->fregs[n - 25] = ldfl_p(mem_buf);
        }
        break;
    case 41:
        env->ssr = ldl_p(mem_buf);
        break;
    case 42:
        env->spc = ldl_p(mem_buf);
        break;
    case 43 ... 50:
        env->gregs[n - 43] = ldl_p(mem_buf);
        break;
    case 51 ... 58:
        env->gregs[n - (51 - 16)] = ldl_p(mem_buf);
        break;
    default:
        return 0;
    }

    return 4;
}
