/*
 *  HPPA emulation cpu helpers for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
#include "qemu/log.h"
#include "cpu.h"
#include "fpu/softfloat.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/qemu-print.h"

target_ulong cpu_hppa_get_psw(CPUHPPAState *env)
{
    target_ulong psw;
    target_ulong mask1 = (target_ulong)-1 / 0xf;
    target_ulong maskf = (target_ulong)-1 / 0xffff * 0xf;

    /* Fold carry bits down to 8 consecutive bits.  */
    /* ^^^b^^^c^^^d^^^e^^^f^^^g^^^h^^^i^^^j^^^k^^^l^^^m^^^n^^^o^^^p^^^^ */
    psw = (env->psw_cb >> 4) & mask1;
    /* .......b...c...d...e...f...g...h...i...j...k...l...m...n...o...p */
    psw |= psw >> 3;
    /* .......b..bc..cd..de..ef..fg..gh..hi..ij..jk..kl..lm..mn..no..op */
    psw |= psw >> 6;
    psw &= maskf;
    /* .............bcd............efgh............ijkl............mnop */
    psw |= psw >> 12;
    /* .............bcd.........bcdefgh........efghijkl........ijklmnop */
    psw |= env->psw_cb_msb << 39;
    /* .............bcd........abcdefgh........efghijkl........ijklmnop */

    /* For hppa64, the two 8-bit fields are discontiguous. */
    if (hppa_is_pa20(env)) {
        psw = (psw & 0xff00000000ull) | ((psw & 0xff) << 8);
    } else {
        psw = (psw & 0xff) << 8;
    }

    psw |= env->psw_n * PSW_N;
    psw |= (env->psw_v < 0) * PSW_V;
    psw |= env->psw;

    return psw;
}

void cpu_hppa_put_psw(CPUHPPAState *env, target_ulong psw)
{
    uint64_t reserved;
    target_ulong cb = 0;

    /* Do not allow reserved bits to be set. */
    if (hppa_is_pa20(env)) {
        reserved = MAKE_64BIT_MASK(40, 24) | MAKE_64BIT_MASK(28, 4);
        reserved |= PSW_G;                  /* PA1.x only */
        reserved |= PSW_E;                  /* not implemented */
    } else {
        reserved = MAKE_64BIT_MASK(32, 32) | MAKE_64BIT_MASK(28, 2);
        reserved |= PSW_O | PSW_W;          /* PA2.0 only */
        reserved |= PSW_E | PSW_Y | PSW_Z;  /* not implemented */
    }
    psw &= ~reserved;

    env->psw = psw & (uint32_t)~(PSW_N | PSW_V | PSW_CB);

    env->psw_n = (psw / PSW_N) & 1;
    env->psw_v = -((psw / PSW_V) & 1);

    env->psw_cb_msb = (psw >> 39) & 1;
    cb |= ((psw >> 38) & 1) << 60;
    cb |= ((psw >> 37) & 1) << 56;
    cb |= ((psw >> 36) & 1) << 52;
    cb |= ((psw >> 35) & 1) << 48;
    cb |= ((psw >> 34) & 1) << 44;
    cb |= ((psw >> 33) & 1) << 40;
    cb |= ((psw >> 32) & 1) << 36;
    cb |= ((psw >> 15) & 1) << 32;
    cb |= ((psw >> 14) & 1) << 28;
    cb |= ((psw >> 13) & 1) << 24;
    cb |= ((psw >> 12) & 1) << 20;
    cb |= ((psw >> 11) & 1) << 16;
    cb |= ((psw >> 10) & 1) << 12;
    cb |= ((psw >>  9) & 1) <<  8;
    cb |= ((psw >>  8) & 1) <<  4;
    env->psw_cb = cb;
}

void hppa_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPUHPPAState *env = cpu_env(cs);
    target_ulong psw = cpu_hppa_get_psw(env);
    target_ulong psw_cb;
    char psw_c[20];
    int i, w;
    uint64_t m;

    if (hppa_is_pa20(env)) {
        w = 16;
        m = UINT64_MAX;
    } else {
        w = 8;
        m = UINT32_MAX;
    }

    qemu_fprintf(f, "IA_F " TARGET_FMT_lx " IA_B " TARGET_FMT_lx
                 " IIR %0*" PRIx64 "\n",
                 hppa_form_gva_psw(psw, env->iasq_f, env->iaoq_f),
                 hppa_form_gva_psw(psw, env->iasq_b, env->iaoq_b),
                 w, m & env->cr[CR_IIR]);

    psw_c[0]  = (psw & PSW_W ? 'W' : '-');
    psw_c[1]  = (psw & PSW_E ? 'E' : '-');
    psw_c[2]  = (psw & PSW_S ? 'S' : '-');
    psw_c[3]  = (psw & PSW_T ? 'T' : '-');
    psw_c[4]  = (psw & PSW_H ? 'H' : '-');
    psw_c[5]  = (psw & PSW_L ? 'L' : '-');
    psw_c[6]  = (psw & PSW_N ? 'N' : '-');
    psw_c[7]  = (psw & PSW_X ? 'X' : '-');
    psw_c[8]  = (psw & PSW_B ? 'B' : '-');
    psw_c[9]  = (psw & PSW_C ? 'C' : '-');
    psw_c[10] = (psw & PSW_V ? 'V' : '-');
    psw_c[11] = (psw & PSW_M ? 'M' : '-');
    psw_c[12] = (psw & PSW_F ? 'F' : '-');
    psw_c[13] = (psw & PSW_R ? 'R' : '-');
    psw_c[14] = (psw & PSW_Q ? 'Q' : '-');
    psw_c[15] = (psw & PSW_P ? 'P' : '-');
    psw_c[16] = (psw & PSW_D ? 'D' : '-');
    psw_c[17] = (psw & PSW_I ? 'I' : '-');
    psw_c[18] = '\0';
    psw_cb = ((env->psw_cb >> 4) & 0x1111111111111111ull)
           | (env->psw_cb_msb << 60);

    qemu_fprintf(f, "PSW  %0*" PRIx64 " CB   %0*" PRIx64 " %s\n",
                 w, m & psw, w, m & psw_cb, psw_c);

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "GR%02d %0*" PRIx64 "%c",
                     i, w, m & env->gr[i],
                     (i & 3) == 3 ? '\n' : ' ');
    }
#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 8; i++) {
        qemu_fprintf(f, "SR%02d %08x%c", i, (uint32_t)(env->sr[i] >> 32),
                     (i & 3) == 3 ? '\n' : ' ');
    }
#endif
     qemu_fprintf(f, "\n");

    /* ??? FR */
}
