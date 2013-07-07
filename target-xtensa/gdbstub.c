/*
 * Xtensa gdb server stub
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

static int cpu_gdb_read_register(CPUXtensaState *env, uint8_t *mem_buf, int n)
{
    const XtensaGdbReg *reg = env->config->gdb_regmap.reg + n;

    if (n < 0 || n >= env->config->gdb_regmap.num_regs) {
        return 0;
    }

    switch (reg->type) {
    case 9: /*pc*/
        GET_REG32(env->pc);

    case 1: /*ar*/
        xtensa_sync_phys_from_window(env);
        GET_REG32(env->phys_regs[(reg->targno & 0xff) % env->config->nareg]);

    case 2: /*SR*/
        GET_REG32(env->sregs[reg->targno & 0xff]);

    case 3: /*UR*/
        GET_REG32(env->uregs[reg->targno & 0xff]);

    case 4: /*f*/
        GET_REG32(float32_val(env->fregs[reg->targno & 0x0f]));

    case 8: /*a*/
        GET_REG32(env->regs[reg->targno & 0x0f]);

    default:
        qemu_log("%s from reg %d of unsupported type %d\n",
                 __func__, n, reg->type);
        return 0;
    }
}

static int cpu_gdb_write_register(CPUXtensaState *env, uint8_t *mem_buf, int n)
{
    uint32_t tmp;
    const XtensaGdbReg *reg = env->config->gdb_regmap.reg + n;

    if (n < 0 || n >= env->config->gdb_regmap.num_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    switch (reg->type) {
    case 9: /*pc*/
        env->pc = tmp;
        break;

    case 1: /*ar*/
        env->phys_regs[(reg->targno & 0xff) % env->config->nareg] = tmp;
        xtensa_sync_window_from_phys(env);
        break;

    case 2: /*SR*/
        env->sregs[reg->targno & 0xff] = tmp;
        break;

    case 3: /*UR*/
        env->uregs[reg->targno & 0xff] = tmp;
        break;

    case 4: /*f*/
        env->fregs[reg->targno & 0x0f] = make_float32(tmp);
        break;

    case 8: /*a*/
        env->regs[reg->targno & 0x0f] = tmp;
        break;

    default:
        qemu_log("%s to reg %d of unsupported type %d\n",
                 __func__, n, reg->type);
        return 0;
    }

    return 4;
}
