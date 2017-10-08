/*
 *  HPPA emulation cpu helpers for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"

target_ulong cpu_hppa_get_psw(CPUHPPAState *env)
{
    target_ulong psw;

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

void cpu_hppa_put_psw(CPUHPPAState *env, target_ulong psw)
{
    target_ulong cb = 0;

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
}

void hppa_cpu_do_interrupt(CPUState *cs)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    int i = cs->exception_index;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count;
        const char *name = "<unknown>";

        switch (i) {
        case EXCP_SYSCALL:
            name = "syscall";
            break;
        case EXCP_SIGSEGV:
            name = "sigsegv";
            break;
        case EXCP_SIGILL:
            name = "sigill";
            break;
        case EXCP_SIGFPE:
            name = "sigfpe";
            break;
        }
        qemu_log("INT %6d: %s ia_f=" TARGET_FMT_lx "\n",
                 ++count, name, env->iaoq_f);
    }
    cs->exception_index = -1;
}

bool hppa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    abort();
    return false;
}

void hppa_cpu_dump_state(CPUState *cs, FILE *f,
                         fprintf_function cpu_fprintf, int flags)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ulong psw = cpu_hppa_get_psw(env);
    target_ulong psw_cb;
    char psw_c[20];
    int i;

    cpu_fprintf(f, "IA_F " TARGET_FMT_lx " IA_B " TARGET_FMT_lx "\n",
                env->iaoq_f, env->iaoq_b);

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

    cpu_fprintf(f, "PSW  " TARGET_FMT_lx " CB   " TARGET_FMT_lx " %s\n",
                psw, psw_cb, psw_c);

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "GR%02d " TARGET_FMT_lx " ", i, env->gr[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        }
    }
    cpu_fprintf(f, "\n");

    /* ??? FR */
}
