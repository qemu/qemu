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
    target_ulong icc = 0;

    icc |= ((int32_t)env->cc_N < 0) << PSR_NEG_SHIFT;
    icc |= ((int32_t)env->cc_V < 0) << PSR_OVF_SHIFT;
    icc |= ((int32_t)env->icc_Z == 0) << PSR_ZERO_SHIFT;
    if (TARGET_LONG_BITS == 64) {
        icc |= extract64(env->icc_C, 32, 1) << PSR_CARRY_SHIFT;
    } else {
        icc |= env->icc_C << PSR_CARRY_SHIFT;
    }

#if !defined(TARGET_SPARC64)
    return env->version | icc |
        (env->psref ? PSR_EF : 0) |
        (env->psrpil << 8) |
        (env->psrs ? PSR_S : 0) |
        (env->psrps ? PSR_PS : 0) |
        (env->psret ? PSR_ET : 0) | env->cwp;
#else
    return icc;
#endif
}

void cpu_put_psr_icc(CPUSPARCState *env, target_ulong val)
{
    if (TARGET_LONG_BITS == 64) {
        /* Do not clobber xcc.[NV] */
        env->cc_N = deposit64(env->cc_N, 0, 32, -(val & PSR_NEG));
        env->cc_V = deposit64(env->cc_V, 0, 32, -(val & PSR_OVF));
        env->icc_C = -(val & PSR_CARRY);
    } else {
        env->cc_N = -(val & PSR_NEG);
        env->cc_V = -(val & PSR_OVF);
        env->icc_C = (val >> PSR_CARRY_SHIFT) & 1;
    }
    env->icc_Z = ~val & PSR_ZERO;
}

void cpu_put_psr_raw(CPUSPARCState *env, target_ulong val)
{
    cpu_put_psr_icc(env, val);
#if !defined(TARGET_SPARC64)
    env->psref = (val & PSR_EF) ? 1 : 0;
    env->psrpil = (val & PSR_PIL) >> 8;
    env->psrs = (val & PSR_S) ? 1 : 0;
    env->psrps = (val & PSR_PS) ? 1 : 0;
    env->psret = (val & PSR_ET) ? 1 : 0;
#endif
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
        bql_lock();
        cpu_put_psr(env, new_psr);
        bql_unlock();
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
    target_ulong ccr = 0;

    ccr |= (env->icc_C >> 32) & 1;
    ccr |= ((int32_t)env->cc_V < 0) << 1;
    ccr |= ((int32_t)env->icc_Z == 0) << 2;
    ccr |= ((int32_t)env->cc_N < 0) << 3;

    ccr |= env->xcc_C << 4;
    ccr |= (env->cc_V < 0) << 5;
    ccr |= (env->xcc_Z == 0) << 6;
    ccr |= (env->cc_N < 0) << 7;

    return ccr;
}

void cpu_put_ccr(CPUSPARCState *env, target_ulong val)
{
    env->cc_N = deposit64(-(val & 0x08), 32, 32, -(val & 0x80));
    env->cc_V = deposit64(-(val & 0x02), 32, 32, -(val & 0x20));
    env->icc_C = (uint64_t)val << 32;
    env->xcc_C = (val >> 4) & 1;
    env->icc_Z = ~val & 0x04;
    env->xcc_Z = ~val & 0x40;
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
        bql_lock();
        cpu_check_irqs(env);
        bql_unlock();
    }
#endif
}

void helper_wrpil(CPUSPARCState *env, target_ulong new_pil)
{
#if !defined(CONFIG_USER_ONLY)
    trace_win_helper_wrpil(env->psrpil, (uint32_t)new_pil);

    env->psrpil = new_pil;

    if (cpu_interrupts_enabled(env)) {
        bql_lock();
        cpu_check_irqs(env);
        bql_unlock();
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
        bql_lock();
        cpu_check_irqs(env);
        bql_unlock();
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
        bql_lock();
        cpu_check_irqs(env);
        bql_unlock();
    }
#endif
}
#endif
