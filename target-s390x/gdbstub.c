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

static int cpu_gdb_read_register(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    uint64_t val;
    int cc_op;

    switch (n) {
    case S390_PSWM_REGNUM:
        cc_op = calc_cc(env, env->cc_op, env->cc_src, env->cc_dst, env->cc_vr);
        val = deposit64(env->psw.mask, 44, 2, cc_op);
        GET_REGL(val);
    case S390_PSWA_REGNUM:
        GET_REGL(env->psw.addr);
    case S390_R0_REGNUM ... S390_R15_REGNUM:
        GET_REGL(env->regs[n-S390_R0_REGNUM]);
    case S390_A0_REGNUM ... S390_A15_REGNUM:
        GET_REG32(env->aregs[n-S390_A0_REGNUM]);
    case S390_FPC_REGNUM:
        GET_REG32(env->fpc);
    case S390_F0_REGNUM ... S390_F15_REGNUM:
        GET_REG64(env->fregs[n-S390_F0_REGNUM].ll);
    }

    return 0;
}

static int cpu_gdb_write_register(CPUS390XState *env, uint8_t *mem_buf, int n)
{
    target_ulong tmpl;
    uint32_t tmp32;
    int r = 8;
    tmpl = ldtul_p(mem_buf);
    tmp32 = ldl_p(mem_buf);

    switch (n) {
    case S390_PSWM_REGNUM:
        env->psw.mask = tmpl;
        env->cc_op = extract64(tmpl, 44, 2);
        break;
    case S390_PSWA_REGNUM:
        env->psw.addr = tmpl;
        break;
    case S390_R0_REGNUM ... S390_R15_REGNUM:
        env->regs[n-S390_R0_REGNUM] = tmpl;
        break;
    case S390_A0_REGNUM ... S390_A15_REGNUM:
        env->aregs[n-S390_A0_REGNUM] = tmp32;
        r = 4;
        break;
    case S390_FPC_REGNUM:
        env->fpc = tmp32;
        r = 4;
        break;
    case S390_F0_REGNUM ... S390_F15_REGNUM:
        env->fregs[n-S390_F0_REGNUM].ll = tmpl;
        break;
    default:
        return 0;
    }
    return r;
}
