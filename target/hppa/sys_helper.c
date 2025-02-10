/*
 * Helpers for HPPA system instructions.
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/timer.h"
#include "system/runstate.h"
#include "system/system.h"
#include "chardev/char-fe.h"

void HELPER(write_interval_timer)(CPUHPPAState *env, target_ulong val)
{
    HPPACPU *cpu = env_archcpu(env);
    uint64_t current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t timeout;

    /*
     * Even in 64-bit mode, the comparator is always 32-bit.  But the
     * value we expose to the guest is 1/4 of the speed of the clock,
     * so moosh in 34 bits.
     */
    timeout = deposit64(current, 0, 34, (uint64_t)val << 2);

    /* If the mooshing puts the clock in the past, advance to next round.  */
    if (timeout < current + 1000) {
        timeout += 1ULL << 34;
    }

    cpu->env.cr[CR_IT] = timeout;
    timer_mod(cpu->alarm_timer, timeout);
}

void HELPER(halt)(CPUHPPAState *env)
{
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    helper_excp(env, EXCP_HLT);
}

void HELPER(reset)(CPUHPPAState *env)
{
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    helper_excp(env, EXCP_HLT);
}

target_ulong HELPER(swap_system_mask)(CPUHPPAState *env, target_ulong nsm)
{
    target_ulong psw = env->psw;
    /*
     * Setting the PSW Q bit to 1, if it was not already 1, is an
     * undefined operation.
     *
     * However, HP-UX 10.20 does this with the SSM instruction.
     * Tested this on HP9000/712 and HP9000/785/C3750 and both
     * machines set the Q bit from 0 to 1 without an exception,
     * so let this go without comment.
     */
    cpu_hppa_put_psw(env, (psw & ~PSW_SM) | (nsm & PSW_SM));
    return psw & PSW_SM;
}

void HELPER(rfi)(CPUHPPAState *env)
{
    uint64_t mask;

    cpu_hppa_put_psw(env, env->cr[CR_IPSW]);

    /*
     * For pa2.0, IIASQ is the top bits of the virtual address.
     * To recreate the space identifier, remove the offset bits.
     * For pa1.x, the mask reduces to no change to space.
     */
    mask = env->gva_offset_mask;

    env->iaoq_f = env->cr[CR_IIAOQ];
    env->iaoq_b = env->cr_back[1];
    env->iasq_f = (env->cr[CR_IIASQ] << 32) & ~(env->iaoq_f & mask);
    env->iasq_b = (env->cr_back[0] << 32) & ~(env->iaoq_b & mask);

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            CPUState *cs = env_cpu(env);

            fprintf(logfile, "RFI: cpu %d\n", cs->cpu_index);
            hppa_cpu_dump_state(cs, logfile, 0);
            qemu_log_unlock(logfile);
        }
    }
}

static void getshadowregs(CPUHPPAState *env)
{
    env->gr[1] = env->shadow[0];
    env->gr[8] = env->shadow[1];
    env->gr[9] = env->shadow[2];
    env->gr[16] = env->shadow[3];
    env->gr[17] = env->shadow[4];
    env->gr[24] = env->shadow[5];
    env->gr[25] = env->shadow[6];
}

void HELPER(rfi_r)(CPUHPPAState *env)
{
    getshadowregs(env);
    helper_rfi(env);
}

#ifndef CONFIG_USER_ONLY
/*
 * diag_console_output() is a helper function used during the initial bootup
 * process of the SeaBIOS-hppa firmware.  During the bootup phase, addresses of
 * serial ports on e.g. PCI busses are unknown and most other devices haven't
 * been initialized and configured yet.  With help of a simple "diag" assembler
 * instruction and an ASCII character code in register %r26 firmware can easily
 * print debug output without any dependencies to the first serial port and use
 * that as serial console.
 */
void HELPER(diag_console_output)(CPUHPPAState *env)
{
    CharBackend *serial_backend;
    Chardev *serial_port;
    unsigned char c;

    /* find first serial port */
    serial_port = serial_hd(0);
    if (!serial_port) {
        return;
    }

    /* get serial_backend for the serial port */
    serial_backend = serial_port->be;
    if (!serial_backend ||
        !qemu_chr_fe_backend_connected(serial_backend)) {
        return;
    }

    c = (unsigned char)env->gr[26];
    qemu_chr_fe_write(serial_backend, &c, sizeof(c));
}
#endif
