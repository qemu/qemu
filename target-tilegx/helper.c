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

#include "cpu.h"
#include "qemu-common.h"
#include "exec/helper-proto.h"

void helper_exception(CPUTLGState *env, uint32_t excp)
{
    CPUState *cs = CPU(tilegx_env_get_cpu(env));

    cs->exception_index = excp;
    cpu_loop_exit(cs);
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
