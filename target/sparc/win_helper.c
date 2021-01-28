/*
 * Helpers for CWP and PSTATE handling
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "trace.h"

static inline void memcpy32(target_ulong *dst, const target_ulong *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
}

void cpu_set_cwp(CPUSPARCState *env, int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env->cwp == env->nwindows - 1) {
        memcpy32(env->regbase, env->regbase + env->nwindows * 16);
    }
    env->cwp = new_cwp;

    /* put the wrap registers at their temporary location */
    if (new_cwp == env->nwindows - 1) {
        memcpy32(env->regbase + env->nwindows * 16, env->regbase);
    }
    env->regwptr = env->regbase + (new_cwp * 16);
}

target_ulong cpu_get_psr(CPUSPARCState *env)
{
    helper_compute_psr(env);

#if !defined(TARGET_SPARC64)
    return env->version | (env->psr & PSR_ICC) |
        (env->psref ? PSR_EF : 0) |
        (env->psrpil << 8) |
        (env->psrs ? PSR_S : 0) |
        (env->psrps ? PSR_PS : 0) |
        (env->psret ? PSR_ET : 0) | env->cwp;
#else
    return env->psr & PSR_ICC;
#endif
}

void cpu_put_psr_raw(CPUSPARCState *env, target_ulong val)
{
    env->psr = val & PSR_ICC;
#if !defined(TARGET_SPARC64)
    env->psref = (val & PSR_EF) ? 1 : 0;
    env->psrpil = (val & PSR_PIL) >> 8;
    env->psrs = (val & PSR_S) ? 1 : 0;
    env->psrps = (val & PSR_PS) ? 1 : 0;
    env->psret = (val & PSR_ET) ? 1 : 0;
#endif
    env->cc_op = CC_OP_FLAGS;
#if !defined(TARGET_SPARC64)
    cpu_set_cwp(env, val & PSR_CWP);
#endif
}

/* Called with BQL held */
void cpu_put_psr(CPUSPARCState *env, target_ulong val)
{
    cpu_put_psr_raw(env, val);
#if ((!defined(TARGET_SPARC64)) && !defined(CONFIG_USER_ONLY))
    cpu_check_irqs(env);
#endif
}

int cpu_cwp_inc(CPUSPARCState *env, int cwp)
{
    if (unlikely(cwp >= env->nwindows)) {
        cwp -= env->nwindows;
    }
    return cwp;
}

int cpu_cwp_dec(CPUSPARCState *env, int cwp)
{
    if (unlikely(cwp < 0)) {
        cwp += env->nwindows;
    }
    return cwp;
}

#ifndef TARGET_SPARC64
void helper_rett(CPUSPARCState *env)
{
    unsigned int cwp;

    if (env->psret == 1) {
        cpu_raise_exception_ra(env, TT_ILL_INSN, GETPC());
    }

    env->psret = 1;
    cwp = cpu_cwp_inc(env, env->cwp + 1) ;
    if (env->wim & (1 << cwp)) {
        cpu_raise_exception_ra(env, TT_WIN_UNF, GETPC());
    }
    cpu_set_cwp(env, cwp);
    env->psrs = env->psrps;
}

/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void helper_save(CPUSPARCState *env)
{
    uint32_t cwp;

    cwp = cpu_cwp_dec(env, env->cwp - 1);
    if (env->wim & (1 << cwp)) {
        cpu_raise_exception_ra(env, TT_WIN_OVF, GETPC());
    }
    cpu_set_cwp(env, cwp);
}

void helper_restore(CPUSPARCState *env)
{
    uint32_t cwp;

    cwp = cpu_cwp_inc(env, env->cwp + 1);
    if (env->wim & (1 << cwp)) {
        cpu_raise_exception_ra(env, TT_WIN_UNF, GETPC());
    }
    cpu_set_cwp(env, cwp);
}

void helper_wrpsr(CPUSPARCState *env, target_ulong new_psr)
{
    if ((new_psr & PSR_CWP) >= env->nwindows) {
        cpu_raise_exception_ra(env, TT_ILL_INSN, GETPC());
    } else {
        /* cpu_put_psr may trigger interrupts, hence BQL */
        qemu_mutex_lock_iothread();
        cpu_put_psr(env, new_psr);
        qemu_mutex_unlock_iothread();
    }
}

target_ulong helper_rdpsr(CPUSPARCState *env)
{
    return cpu_get_psr(env);
}

#else
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void helper_save(CPUSPARCState *env)
{
    uint32_t cwp;

    cwp = cpu_cwp_dec(env, env->cwp - 1);
    if (env->cansave == 0) {
        int tt = TT_SPILL | (env->otherwin != 0
                             ? (TT_WOTHER | ((env->wstate & 0x38) >> 1))
                             : ((env->wstate & 0x7) << 2));
        cpu_raise_exception_ra(env, tt, GETPC());
    } else {
        if (env->cleanwin - env->canrestore == 0) {
            /* XXX Clean windows without trap */
            cpu_raise_exception_ra(env, TT_CLRWIN, GETPC());
        } else {
            env->cansave--;
            env->canrestore++;
            cpu_set_cwp(env, cwp);
        }
    }
}

void helper_restore(CPUSPARCState *env)
{
    uint32_t cwp;

    cwp = cpu_cwp_inc(env, env->cwp + 1);
    if (env->canrestore == 0) {
        int tt = TT_FILL | (env->otherwin != 0
                            ? (TT_WOTHER | ((env->wstate & 0x38) >> 1))
                            : ((env->wstate & 0x7) << 2));
        cpu_raise_exception_ra(env, tt, GETPC());
    } else {
        env->cansave++;
        env->canrestore--;
        cpu_set_cwp(env, cwp);
    }
}

void helper_flushw(CPUSPARCState *env)
{
    if (env->cansave != env->nwindows - 2) {
        int tt = TT_SPILL | (env->otherwin != 0
                             ? (TT_WOTHER | ((env->wstate & 0x38) >> 1))
                             : ((env->wstate & 0x7) << 2));
        cpu_raise_exception_ra(env, tt, GETPC());
    }
}

void helper_saved(CPUSPARCState *env)
{
    env->cansave++;
    if (env->otherwin == 0) {
        env->canrestore--;
    } else {
        env->otherwin--;
    }
}

void helper_restored(CPUSPARCState *env)
{
    env->canrestore++;
    if (env->cleanwin < env->nwindows - 1) {
        env->cleanwin++;
    }
    if (env->otherwin == 0) {
        env->cansave--;
    } else {
        env->otherwin--;
    }
}

target_ulong cpu_get_ccr(CPUSPARCState *env)
{
    target_ulong psr;

    psr = cpu_get_psr(env);

    return ((env->xcc >> 20) << 4) | ((psr & PSR_ICC) >> 20);
}

void cpu_put_ccr(CPUSPARCState *env, target_ulong val)
{
    env->xcc = (val >> 4) << 20;
    env->psr = (val & 0xf) << 20;
    CC_OP = CC_OP_FLAGS;
}

target_ulong cpu_get_cwp64(CPUSPARCState *env)
{
    return env->nwindows - 1 - env->cwp;
}

void cpu_put_cwp64(CPUSPARCState *env, int cwp)
{
    if (unlikely(cwp >= env->nwindows || cwp < 0)) {
        cwp %= env->nwindows;
    }
    cpu_set_cwp(env, env->nwindows - 1 - cwp);
}

target_ulong helper_rdccr(CPUSPARCState *env)
{
    return cpu_get_ccr(env);
}

void helper_wrccr(CPUSPARCState *env, target_ulong new_ccr)
{
    cpu_put_ccr(env, new_ccr);
}

/* CWP handling is reversed in V9, but we still use the V8 register
   order. */
target_ulong helper_rdcwp(CPUSPARCState *env)
{
    return cpu_get_cwp64(env);
}

void helper_wrcwp(CPUSPARCState *env, target_ulong new_cwp)
{
    cpu_put_cwp64(env, new_cwp);
}

static inline uint64_t *get_gregset(CPUSPARCState *env, uint32_t pstate)
{
    if (env->def.features & CPU_FEATURE_GL) {
        return env->glregs + (env->gl & 7) * 8;
    }

    switch (pstate) {
    default:
        trace_win_helper_gregset_error(pstate);
        /* fall through to normal set of global registers */
    case 0:
        return env->bgregs;
    case PS_AG:
        return env->agregs;
    case PS_MG:
        return env->mgregs;
    case PS_IG:
        return env->igregs;
    }
}

static inline uint64_t *get_gl_gregset(CPUSPARCState *env, uint32_t gl)
{
    return env->glregs + (gl & 7) * 8;
}

/* Switch global register bank */
void cpu_gl_switch_gregs(CPUSPARCState *env, uint32_t new_gl)
{
    uint64_t *src, *dst;
    src = get_gl_gregset(env, new_gl);
    dst = get_gl_gregset(env, env->gl);

    if (src != dst) {
        memcpy32(dst, env->gregs);
        memcpy32(env->gregs, src);
    }
}

void helper_wrgl(CPUSPARCState *env, target_ulong new_gl)
{
    cpu_gl_switch_gregs(env, new_gl & 7);
    env->gl = new_gl & 7;
}

void cpu_change_pstate(CPUSPARCState *env, uint32_t new_pstate)
{
    uint32_t pstate_regs, new_pstate_regs;
    uint64_t *src, *dst;

    if (env->def.features & CPU_FEATURE_GL) {
        /* PS_AG, IG and MG are not implemented in this case */
        new_pstate &= ~(PS_AG | PS_IG | PS_MG);
        env->pstate = new_pstate;
        return;
    }

    pstate_regs = env->pstate & 0xc01;
    new_pstate_regs = new_pstate & 0xc01;

    if (new_pstate_regs != pstate_regs) {
        trace_win_helper_switch_pstate(pstate_regs, new_pstate_regs);

        /* Switch global register bank */
        src = get_gregset(env, new_pstate_regs);
        dst = get_gregset(env, pstate_regs);
        memcpy32(dst, env->gregs);
        memcpy32(env->gregs, src);
    } else {
        trace_win_helper_no_switch_pstate(new_pstate_regs);
    }
    env->pstate = new_pstate;
}

void helper_wrpstate(CPUSPARCState *env, target_ulong new_state)
{
    cpu_change_pstate(env, new_state & 0xf3f);

#if !defined(CONFIG_USER_ONLY)
    if (cpu_interrupts_enabled(env)) {
        qemu_mutex_lock_iothread();
        cpu_check_irqs(env);
        qemu_mutex_unlock_iothread();
    }
#endif
}

void helper_wrpil(CPUSPARCState *env, target_ulong new_pil)
{
#if !defined(CONFIG_USER_ONLY)
    trace_win_helper_wrpil(env->psrpil, (uint32_t)new_pil);

    env->psrpil = new_pil;

    if (cpu_interrupts_enabled(env)) {
        qemu_mutex_lock_iothread();
        cpu_check_irqs(env);
        qemu_mutex_unlock_iothread();
    }
#endif
}

void helper_done(CPUSPARCState *env)
{
    trap_state *tsptr = cpu_tsptr(env);

    env->pc = tsptr->tnpc;
    env->npc = tsptr->tnpc + 4;
    cpu_put_ccr(env, tsptr->tstate >> 32);
    env->asi = (tsptr->tstate >> 24) & 0xff;
    cpu_change_pstate(env, (tsptr->tstate >> 8) & 0xf3f);
    cpu_put_cwp64(env, tsptr->tstate & 0xff);
    if (cpu_has_hypervisor(env)) {
        uint32_t new_gl = (tsptr->tstate >> 40) & 7;
        env->hpstate = env->htstate[env->tl];
        cpu_gl_switch_gregs(env, new_gl);
        env->gl = new_gl;
    }
    env->tl--;

    trace_win_helper_done(env->tl);

#if !defined(CONFIG_USER_ONLY)
    if (cpu_interrupts_enabled(env)) {
        qemu_mutex_lock_iothread();
        cpu_check_irqs(env);
        qemu_mutex_unlock_iothread();
    }
#endif
}

void helper_retry(CPUSPARCState *env)
{
    trap_state *tsptr = cpu_tsptr(env);

    env->pc = tsptr->tpc;
    env->npc = tsptr->tnpc;
    cpu_put_ccr(env, tsptr->tstate >> 32);
    env->asi = (tsptr->tstate >> 24) & 0xff;
    cpu_change_pstate(env, (tsptr->tstate >> 8) & 0xf3f);
    cpu_put_cwp64(env, tsptr->tstate & 0xff);
    if (cpu_has_hypervisor(env)) {
        uint32_t new_gl = (tsptr->tstate >> 40) & 7;
        env->hpstate = env->htstate[env->tl];
        cpu_gl_switch_gregs(env, new_gl);
        env->gl = new_gl;
    }
    env->tl--;

    trace_win_helper_retry(env->tl);

#if !defined(CONFIG_USER_ONLY)
    if (cpu_interrupts_enabled(env)) {
        qemu_mutex_lock_iothread();
        cpu_check_irqs(env);
        qemu_mutex_unlock_iothread();
    }
#endif
}
#endif
