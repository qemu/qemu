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

target_ureg cpu_hppa_get_psw(CPUHPPAState *env)
{
    target_ureg psw;

    /* Fold carry bits down to 8 consecutive bits.  */
    /* ??? Needs tweaking for hppa64.  */
    /* .......b...c...d...e...f...g...h */
    psw = (env->psw_cb >> 4) & 0x01111111;
    /* .......b..bc..cd..de..ef..fg..gh */
    psw |= psw >> 3;
    /* .............bcd............efgh */
    psw |= (psw >> 6) & 0x000f000f;
    /* .........................bcdefgh */
    psw |= (psw >> 12) & 0xf;
    psw |= env->psw_cb_msb << 7;
    psw = (psw & 0xff) << 8;

    psw |= env->psw_n * PSW_N;
    psw |= (env->psw_v < 0) * PSW_V;
    psw |= env->psw;

    return psw;
}

void cpu_hppa_put_psw(CPUHPPAState *env, target_ureg psw)
{
    target_ureg old_psw = env->psw;
    target_ureg cb = 0;

    env->psw = psw & ~(PSW_N | PSW_V | PSW_CB);
    env->psw_n = (psw / PSW_N) & 1;
    env->psw_v = -((psw / PSW_V) & 1);
    env->psw_cb_msb = (psw >> 15) & 1;

    cb |= ((psw >> 14) & 1) << 28;
    cb |= ((psw >> 13) & 1) << 24;
    cb |= ((psw >> 12) & 1) << 20;
    cb |= ((psw >> 11) & 1) << 16;
    cb |= ((psw >> 10) & 1) << 12;
    cb |= ((psw >>  9) & 1) <<  8;
    cb |= ((psw >>  8) & 1) <<  4;
    env->psw_cb = cb;

    /* If PSW_P changes, it affects how we translate addresses.  */
    if ((psw ^ old_psw) & PSW_P) {
#ifndef CONFIG_USER_ONLY
        tlb_flush_by_mmuidx(env_cpu(env), 0xf);
#endif
    }
}

void hppa_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ureg psw = cpu_hppa_get_psw(env);
    target_ureg psw_cb;
    char psw_c[20];
    int i;

    qemu_fprintf(f, "IA_F " TARGET_FMT_lx " IA_B " TARGET_FMT_lx "\n",
                 hppa_form_gva_psw(psw, env->iasq_f, env->iaoq_f),
                 hppa_form_gva_psw(psw, env->iasq_b, env->iaoq_b));

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
    psw_cb = ((env->psw_cb >> 4) & 0x01111111) | (env->psw_cb_msb << 28);

    qemu_fprintf(f, "PSW  " TREG_FMT_lx " CB   " TREG_FMT_lx " %s\n",
                 psw, psw_cb, psw_c);

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "GR%02d " TREG_FMT_lx "%c", i, env->gr[i],
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
