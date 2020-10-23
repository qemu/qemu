/*
 * Xtensa gdb server stub
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
#include "qemu/log.h"

enum {
  xtRegisterTypeArRegfile = 1,  /* Register File ar0..arXX.  */
  xtRegisterTypeSpecialReg,     /* CPU states, such as PS, Booleans, (rsr).  */
  xtRegisterTypeUserReg,        /* User defined registers (rur).  */
  xtRegisterTypeTieRegfile,     /* User define register files.  */
  xtRegisterTypeTieState,       /* TIE States (mapped on user regs).  */
  xtRegisterTypeMapped,         /* Mapped on Special Registers.  */
  xtRegisterTypeUnmapped,       /* Special case of masked registers.  */
  xtRegisterTypeWindow,         /* Live window registers (a0..a15).  */
  xtRegisterTypeVirtual,        /* PC, FP.  */
  xtRegisterTypeUnknown
};

#define XTENSA_REGISTER_FLAGS_PRIVILEGED        0x0001
#define XTENSA_REGISTER_FLAGS_READABLE          0x0002
#define XTENSA_REGISTER_FLAGS_WRITABLE          0x0004
#define XTENSA_REGISTER_FLAGS_VOLATILE          0x0008

void xtensa_count_regs(const XtensaConfig *config,
                       unsigned *n_regs, unsigned *n_core_regs)
{
    unsigned i;
    bool count_core_regs = true;

    for (i = 0; config->gdb_regmap.reg[i].targno >= 0; ++i) {
        if (config->gdb_regmap.reg[i].type != xtRegisterTypeTieState &&
            config->gdb_regmap.reg[i].type != xtRegisterTypeMapped &&
            config->gdb_regmap.reg[i].type != xtRegisterTypeUnmapped) {
            ++*n_regs;
            if (count_core_regs) {
                if ((config->gdb_regmap.reg[i].flags &
                     XTENSA_REGISTER_FLAGS_PRIVILEGED) == 0) {
                    ++*n_core_regs;
                } else {
                    count_core_regs = false;
                }
            }
        }
    }
}

int xtensa_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;
    const XtensaGdbReg *reg = env->config->gdb_regmap.reg + n;
#ifdef CONFIG_USER_ONLY
    int num_regs = env->config->gdb_regmap.num_core_regs;
#else
    int num_regs = env->config->gdb_regmap.num_regs;
#endif
    unsigned i;

    if (n < 0 || n >= num_regs) {
        return 0;
    }

    switch (reg->type) {
    case xtRegisterTypeVirtual: /*pc*/
        return gdb_get_reg32(mem_buf, env->pc);

    case xtRegisterTypeArRegfile: /*ar*/
        xtensa_sync_phys_from_window(env);
        return gdb_get_reg32(mem_buf, env->phys_regs[(reg->targno & 0xff)
                                                     % env->config->nareg]);

    case xtRegisterTypeSpecialReg: /*SR*/
        return gdb_get_reg32(mem_buf, env->sregs[reg->targno & 0xff]);

    case xtRegisterTypeUserReg: /*UR*/
        return gdb_get_reg32(mem_buf, env->uregs[reg->targno & 0xff]);

    case xtRegisterTypeTieRegfile: /*f*/
        i = reg->targno & 0x0f;
        switch (reg->size) {
        case 4:
            return gdb_get_reg32(mem_buf,
                                 float32_val(env->fregs[i].f32[FP_F32_LOW]));
        case 8:
            return gdb_get_reg64(mem_buf, float64_val(env->fregs[i].f64));
        default:
            qemu_log_mask(LOG_UNIMP, "%s from reg %d of unsupported size %d\n",
                          __func__, n, reg->size);
            return gdb_get_zeroes(mem_buf, reg->size);
        }

    case xtRegisterTypeWindow: /*a*/
        return gdb_get_reg32(mem_buf, env->regs[reg->targno & 0x0f]);

    default:
        qemu_log_mask(LOG_UNIMP, "%s from reg %d of unsupported type %d\n",
                      __func__, n, reg->type);
        return gdb_get_zeroes(mem_buf, reg->size);
    }
}

int xtensa_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;
    uint32_t tmp;
    const XtensaGdbReg *reg = env->config->gdb_regmap.reg + n;
#ifdef CONFIG_USER_ONLY
    int num_regs = env->config->gdb_regmap.num_core_regs;
#else
    int num_regs = env->config->gdb_regmap.num_regs;
#endif

    if (n < 0 || n >= num_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    switch (reg->type) {
    case xtRegisterTypeVirtual: /*pc*/
        env->pc = tmp;
        break;

    case xtRegisterTypeArRegfile: /*ar*/
        env->phys_regs[(reg->targno & 0xff) % env->config->nareg] = tmp;
        xtensa_sync_window_from_phys(env);
        break;

    case xtRegisterTypeSpecialReg: /*SR*/
        env->sregs[reg->targno & 0xff] = tmp;
        break;

    case xtRegisterTypeUserReg: /*UR*/
        env->uregs[reg->targno & 0xff] = tmp;
        break;

    case xtRegisterTypeTieRegfile: /*f*/
        switch (reg->size) {
        case 4:
            env->fregs[reg->targno & 0x0f].f32[FP_F32_LOW] = make_float32(tmp);
            return 4;
        case 8:
            env->fregs[reg->targno & 0x0f].f64 = make_float64(tmp);
            return 8;
        default:
            qemu_log_mask(LOG_UNIMP, "%s to reg %d of unsupported size %d\n",
                          __func__, n, reg->size);
            return reg->size;
        }

    case xtRegisterTypeWindow: /*a*/
        env->regs[reg->targno & 0x0f] = tmp;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s to reg %d of unsupported type %d\n",
                      __func__, n, reg->type);
        return reg->size;
    }

    return 4;
}
