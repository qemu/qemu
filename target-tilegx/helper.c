/*
 * QEMU TILE-Gx helpers
 *
 *  Copyright (c) 2015 Chen Gang
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu-common.h"
#include "exec/helper-proto.h"
#include <zlib.h> /* For crc32 */
#include "syscall_defs.h"

void helper_exception(CPUTLGState *env, uint32_t excp)
{
    CPUState *cs = CPU(tilegx_env_get_cpu(env));

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

void helper_ext01_ics(CPUTLGState *env)
{
    uint64_t val = env->spregs[TILEGX_SPR_EX_CONTEXT_0_1];

    switch (val) {
    case 0:
    case 1:
        env->spregs[TILEGX_SPR_CRITICAL_SEC] = val;
        break;
    default:
#if defined(CONFIG_USER_ONLY)
        env->signo = TARGET_SIGILL;
        env->sigcode = TARGET_ILL_ILLOPC;
        helper_exception(env, TILEGX_EXCP_SIGNAL);
#else
        helper_exception(env, TILEGX_EXCP_OPCODE_UNIMPLEMENTED);
#endif
        break;
    }
}

uint64_t helper_cntlz(uint64_t arg)
{
    return clz64(arg);
}

uint64_t helper_cnttz(uint64_t arg)
{
    return ctz64(arg);
}

uint64_t helper_pcnt(uint64_t arg)
{
    return ctpop64(arg);
}

uint64_t helper_revbits(uint64_t arg)
{
    return revbit64(arg);
}

/*
 * Functional Description
 *     uint64_t a = rf[SrcA];
 *     uint64_t b = rf[SrcB];
 *     uint64_t d = rf[Dest];
 *     uint64_t output = 0;
 *     unsigned int counter;
 *     for (counter = 0; counter < (WORD_SIZE / BYTE_SIZE); counter++)
 *     {
 *         int sel = getByte (b, counter) & 0xf;
 *         uint8_t byte = (sel < 8) ? getByte (d, sel) : getByte (a, (sel - 8));
 *         output = setByte (output, counter, byte);
 *     }
 *     rf[Dest] = output;
 */
uint64_t helper_shufflebytes(uint64_t dest, uint64_t srca, uint64_t srcb)
{
    uint64_t vdst = 0;
    int count;

    for (count = 0; count < 64; count += 8) {
        uint64_t sel = srcb >> count;
        uint64_t src = (sel & 8) ? srca : dest;
        vdst |= extract64(src, (sel & 7) * 8, 8) << count;
    }

    return vdst;
}

uint64_t helper_crc32_8(uint64_t accum, uint64_t input)
{
    uint8_t buf = input;

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(accum ^ 0xffffffff, &buf, 1) ^ 0xffffffff;
}

uint64_t helper_crc32_32(uint64_t accum, uint64_t input)
{
    uint8_t buf[4];

    stl_le_p(buf, input);

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(accum ^ 0xffffffff, buf, 4) ^ 0xffffffff;
}

uint64_t helper_cmula(uint64_t srcd, uint64_t srca, uint64_t srcb)
{
    uint32_t reala = (int16_t)srca;
    uint32_t imaga = (int16_t)(srca >> 16);
    uint32_t realb = (int16_t)srcb;
    uint32_t imagb = (int16_t)(srcb >> 16);
    uint32_t reald = srcd;
    uint32_t imagd = srcd >> 32;
    uint32_t realr = reala * realb - imaga * imagb + reald;
    uint32_t imagr = reala * imagb + imaga * realb + imagd;

    return deposit64(realr, 32, 32, imagr);
}

uint64_t helper_cmulaf(uint64_t srcd, uint64_t srca, uint64_t srcb)
{
    uint32_t reala = (int16_t)srca;
    uint32_t imaga = (int16_t)(srca >> 16);
    uint32_t realb = (int16_t)srcb;
    uint32_t imagb = (int16_t)(srcb >> 16);
    uint32_t reald = (int16_t)srcd;
    uint32_t imagd = (int16_t)(srcd >> 16);
    int32_t realr = reala * realb - imaga * imagb;
    int32_t imagr = reala * imagb + imaga * realb;

    return deposit32((realr >> 15) + reald, 16, 16, (imagr >> 15) + imagd);
}

uint64_t helper_cmul2(uint64_t srca, uint64_t srcb, int shift, int round)
{
    uint32_t reala = (int16_t)srca;
    uint32_t imaga = (int16_t)(srca >> 16);
    uint32_t realb = (int16_t)srcb;
    uint32_t imagb = (int16_t)(srcb >> 16);
    int32_t realr = reala * realb - imaga * imagb + round;
    int32_t imagr = reala * imagb + imaga * realb + round;

    return deposit32(realr >> shift, 16, 16, imagr >> shift);
}
