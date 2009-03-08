/*
 *  MIPS emulation helpers for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include <stdlib.h>
#include "exec.h"

#include "host-utils.h"

#include "helper.h"
/*****************************************************************************/
/* Exceptions processing helpers */

void helper_raise_exception_err (uint32_t exception, int error_code)
{
#if 1
    if (exception < 0x100)
        qemu_log("%s: %d %d\n", __func__, exception, error_code);
#endif
    env->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit();
}

void helper_raise_exception (uint32_t exception)
{
    helper_raise_exception_err(exception, 0);
}

void helper_interrupt_restart (void)
{
    if (!(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM) &&
        (env->CP0_Status & (1 << CP0St_IE)) &&
        (env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask)) {
        env->CP0_Cause &= ~(0x1f << CP0Ca_EC);
        helper_raise_exception(EXCP_EXT_INTERRUPT);
    }
}

#if !defined(CONFIG_USER_ONLY)
static void do_restore_state (void *pc_ptr)
{
    TranslationBlock *tb;
    unsigned long pc = (unsigned long) pc_ptr;
    
    tb = tb_find_pc (pc);
    if (tb) {
        cpu_restore_state (tb, env, pc, NULL);
    }
}
#endif

target_ulong helper_clo (target_ulong t0)
{
    return clo32(t0);
}

target_ulong helper_clz (target_ulong t0)
{
    return clz32(t0);
}

#if defined(TARGET_MIPS64)
target_ulong helper_dclo (target_ulong t0)
{
    return clo64(t0);
}

target_ulong helper_dclz (target_ulong t0)
{
    return clz64(t0);
}
#endif /* TARGET_MIPS64 */

/* 64 bits arithmetic for 32 bits hosts */
static inline uint64_t get_HILO (void)
{
    return ((uint64_t)(env->active_tc.HI[0]) << 32) | (uint32_t)env->active_tc.LO[0];
}

static inline void set_HILO (uint64_t HILO)
{
    env->active_tc.LO[0] = (int32_t)HILO;
    env->active_tc.HI[0] = (int32_t)(HILO >> 32);
}

static inline void set_HIT0_LO (target_ulong t0, uint64_t HILO)
{
    env->active_tc.LO[0] = (int32_t)(HILO & 0xFFFFFFFF);
    t0 = env->active_tc.HI[0] = (int32_t)(HILO >> 32);
}

static inline void set_HI_LOT0 (target_ulong t0, uint64_t HILO)
{
    t0 = env->active_tc.LO[0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->active_tc.HI[0] = (int32_t)(HILO >> 32);
}

#if TARGET_LONG_BITS > HOST_LONG_BITS
void helper_madd (target_ulong t0, target_ulong t1)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1);
    set_HILO((int64_t)get_HILO() + tmp);
}

void helper_maddu (target_ulong t0, target_ulong t1)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1);
    set_HILO(get_HILO() + tmp);
}

void helper_msub (target_ulong t0, target_ulong t1)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1);
    set_HILO((int64_t)get_HILO() - tmp);
}

void helper_msubu (target_ulong t0, target_ulong t1)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1);
    set_HILO(get_HILO() - tmp);
}
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

/* Multiplication variants of the vr54xx. */
target_ulong helper_muls (target_ulong t0, target_ulong t1)
{
    set_HI_LOT0(t0, 0 - ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1));

    return t0;
}

target_ulong helper_mulsu (target_ulong t0, target_ulong t1)
{
    set_HI_LOT0(t0, 0 - ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1));

    return t0;
}

target_ulong helper_macc (target_ulong t0, target_ulong t1)
{
    set_HI_LOT0(t0, ((int64_t)get_HILO()) + ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1));

    return t0;
}

target_ulong helper_macchi (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, ((int64_t)get_HILO()) + ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1));

    return t0;
}

target_ulong helper_maccu (target_ulong t0, target_ulong t1)
{
    set_HI_LOT0(t0, ((uint64_t)get_HILO()) + ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1));

    return t0;
}

target_ulong helper_macchiu (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, ((uint64_t)get_HILO()) + ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1));

    return t0;
}

target_ulong helper_msac (target_ulong t0, target_ulong t1)
{
    set_HI_LOT0(t0, ((int64_t)get_HILO()) - ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1));

    return t0;
}

target_ulong helper_msachi (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, ((int64_t)get_HILO()) - ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1));

    return t0;
}

target_ulong helper_msacu (target_ulong t0, target_ulong t1)
{
    set_HI_LOT0(t0, ((uint64_t)get_HILO()) - ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1));

    return t0;
}

target_ulong helper_msachiu (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, ((uint64_t)get_HILO()) - ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1));

    return t0;
}

target_ulong helper_mulhi (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, (int64_t)(int32_t)t0 * (int64_t)(int32_t)t1);

    return t0;
}

target_ulong helper_mulhiu (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, (uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1);

    return t0;
}

target_ulong helper_mulshi (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, 0 - ((int64_t)(int32_t)t0 * (int64_t)(int32_t)t1));

    return t0;
}

target_ulong helper_mulshiu (target_ulong t0, target_ulong t1)
{
    set_HIT0_LO(t0, 0 - ((uint64_t)(uint32_t)t0 * (uint64_t)(uint32_t)t1));

    return t0;
}

#ifdef TARGET_MIPS64
void helper_dmult (target_ulong t0, target_ulong t1)
{
    muls64(&(env->active_tc.LO[0]), &(env->active_tc.HI[0]), t0, t1);
}

void helper_dmultu (target_ulong t0, target_ulong t1)
{
    mulu64(&(env->active_tc.LO[0]), &(env->active_tc.HI[0]), t0, t1);
}
#endif

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK(v) ((v) & 3)
#define GET_OFFSET(addr, offset) (addr + (offset))
#else
#define GET_LMASK(v) (((v) & 3) ^ 3)
#define GET_OFFSET(addr, offset) (addr - (offset))
#endif

target_ulong helper_lwl(target_ulong t0, target_ulong t1, int mem_idx)
{
    target_ulong tmp;

#ifdef CONFIG_USER_ONLY
#define ldfun ldub_raw
#else
    int (*ldfun)(target_ulong);

    switch (mem_idx)
    {
    case 0: ldfun = ldub_kernel; break;
    case 1: ldfun = ldub_super; break;
    default:
    case 2: ldfun = ldub_user; break;
    }
#endif
    tmp = ldfun(t0);
    t1 = (t1 & 0x00FFFFFF) | (tmp << 24);

    if (GET_LMASK(t0) <= 2) {
        tmp = ldfun(GET_OFFSET(t0, 1));
        t1 = (t1 & 0xFF00FFFF) | (tmp << 16);
    }

    if (GET_LMASK(t0) <= 1) {
        tmp = ldfun(GET_OFFSET(t0, 2));
        t1 = (t1 & 0xFFFF00FF) | (tmp << 8);
    }

    if (GET_LMASK(t0) == 0) {
        tmp = ldfun(GET_OFFSET(t0, 3));
        t1 = (t1 & 0xFFFFFF00) | tmp;
    }
    return (int32_t)t1;
}

target_ulong helper_lwr(target_ulong t0, target_ulong t1, int mem_idx)
{
    target_ulong tmp;

#ifdef CONFIG_USER_ONLY
#define ldfun ldub_raw
#else
    int (*ldfun)(target_ulong);

    switch (mem_idx)
    {
    case 0: ldfun = ldub_kernel; break;
    case 1: ldfun = ldub_super; break;
    default:
    case 2: ldfun = ldub_user; break;
    }
#endif
    tmp = ldfun(t0);
    t1 = (t1 & 0xFFFFFF00) | tmp;

    if (GET_LMASK(t0) >= 1) {
        tmp = ldfun(GET_OFFSET(t0, -1));
        t1 = (t1 & 0xFFFF00FF) | (tmp << 8);
    }

    if (GET_LMASK(t0) >= 2) {
        tmp = ldfun(GET_OFFSET(t0, -2));
        t1 = (t1 & 0xFF00FFFF) | (tmp << 16);
    }

    if (GET_LMASK(t0) == 3) {
        tmp = ldfun(GET_OFFSET(t0, -3));
        t1 = (t1 & 0x00FFFFFF) | (tmp << 24);
    }
    return (int32_t)t1;
}

void helper_swl(target_ulong t0, target_ulong t1, int mem_idx)
{
#ifdef CONFIG_USER_ONLY
#define stfun stb_raw
#else
    void (*stfun)(target_ulong, int);

    switch (mem_idx)
    {
    case 0: stfun = stb_kernel; break;
    case 1: stfun = stb_super; break;
    default:
    case 2: stfun = stb_user; break;
    }
#endif
    stfun(t0, (uint8_t)(t1 >> 24));

    if (GET_LMASK(t0) <= 2)
        stfun(GET_OFFSET(t0, 1), (uint8_t)(t1 >> 16));

    if (GET_LMASK(t0) <= 1)
        stfun(GET_OFFSET(t0, 2), (uint8_t)(t1 >> 8));

    if (GET_LMASK(t0) == 0)
        stfun(GET_OFFSET(t0, 3), (uint8_t)t1);
}

void helper_swr(target_ulong t0, target_ulong t1, int mem_idx)
{
#ifdef CONFIG_USER_ONLY
#define stfun stb_raw
#else
    void (*stfun)(target_ulong, int);

    switch (mem_idx)
    {
    case 0: stfun = stb_kernel; break;
    case 1: stfun = stb_super; break;
    default:
    case 2: stfun = stb_user; break;
    }
#endif
    stfun(t0, (uint8_t)t1);

    if (GET_LMASK(t0) >= 1)
        stfun(GET_OFFSET(t0, -1), (uint8_t)(t1 >> 8));

    if (GET_LMASK(t0) >= 2)
        stfun(GET_OFFSET(t0, -2), (uint8_t)(t1 >> 16));

    if (GET_LMASK(t0) == 3)
        stfun(GET_OFFSET(t0, -3), (uint8_t)(t1 >> 24));
}

#if defined(TARGET_MIPS64)
/* "half" load and stores.  We must do the memory access inline,
   or fault handling won't work.  */

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK64(v) ((v) & 7)
#else
#define GET_LMASK64(v) (((v) & 7) ^ 7)
#endif

target_ulong helper_ldl(target_ulong t0, target_ulong t1, int mem_idx)
{
    uint64_t tmp;

#ifdef CONFIG_USER_ONLY
#define ldfun ldub_raw
#else
    int (*ldfun)(target_ulong);

    switch (mem_idx)
    {
    case 0: ldfun = ldub_kernel; break;
    case 1: ldfun = ldub_super; break;
    default:
    case 2: ldfun = ldub_user; break;
    }
#endif
    tmp = ldfun(t0);
    t1 = (t1 & 0x00FFFFFFFFFFFFFFULL) | (tmp << 56);

    if (GET_LMASK64(t0) <= 6) {
        tmp = ldfun(GET_OFFSET(t0, 1));
        t1 = (t1 & 0xFF00FFFFFFFFFFFFULL) | (tmp << 48);
    }

    if (GET_LMASK64(t0) <= 5) {
        tmp = ldfun(GET_OFFSET(t0, 2));
        t1 = (t1 & 0xFFFF00FFFFFFFFFFULL) | (tmp << 40);
    }

    if (GET_LMASK64(t0) <= 4) {
        tmp = ldfun(GET_OFFSET(t0, 3));
        t1 = (t1 & 0xFFFFFF00FFFFFFFFULL) | (tmp << 32);
    }

    if (GET_LMASK64(t0) <= 3) {
        tmp = ldfun(GET_OFFSET(t0, 4));
        t1 = (t1 & 0xFFFFFFFF00FFFFFFULL) | (tmp << 24);
    }

    if (GET_LMASK64(t0) <= 2) {
        tmp = ldfun(GET_OFFSET(t0, 5));
        t1 = (t1 & 0xFFFFFFFFFF00FFFFULL) | (tmp << 16);
    }

    if (GET_LMASK64(t0) <= 1) {
        tmp = ldfun(GET_OFFSET(t0, 6));
        t1 = (t1 & 0xFFFFFFFFFFFF00FFULL) | (tmp << 8);
    }

    if (GET_LMASK64(t0) == 0) {
        tmp = ldfun(GET_OFFSET(t0, 7));
        t1 = (t1 & 0xFFFFFFFFFFFFFF00ULL) | tmp;
    }

    return t1;
}

target_ulong helper_ldr(target_ulong t0, target_ulong t1, int mem_idx)
{
    uint64_t tmp;

#ifdef CONFIG_USER_ONLY
#define ldfun ldub_raw
#else
    int (*ldfun)(target_ulong);

    switch (mem_idx)
    {
    case 0: ldfun = ldub_kernel; break;
    case 1: ldfun = ldub_super; break;
    default:
    case 2: ldfun = ldub_user; break;
    }
#endif
    tmp = ldfun(t0);
    t1 = (t1 & 0xFFFFFFFFFFFFFF00ULL) | tmp;

    if (GET_LMASK64(t0) >= 1) {
        tmp = ldfun(GET_OFFSET(t0, -1));
        t1 = (t1 & 0xFFFFFFFFFFFF00FFULL) | (tmp  << 8);
    }

    if (GET_LMASK64(t0) >= 2) {
        tmp = ldfun(GET_OFFSET(t0, -2));
        t1 = (t1 & 0xFFFFFFFFFF00FFFFULL) | (tmp << 16);
    }

    if (GET_LMASK64(t0) >= 3) {
        tmp = ldfun(GET_OFFSET(t0, -3));
        t1 = (t1 & 0xFFFFFFFF00FFFFFFULL) | (tmp << 24);
    }

    if (GET_LMASK64(t0) >= 4) {
        tmp = ldfun(GET_OFFSET(t0, -4));
        t1 = (t1 & 0xFFFFFF00FFFFFFFFULL) | (tmp << 32);
    }

    if (GET_LMASK64(t0) >= 5) {
        tmp = ldfun(GET_OFFSET(t0, -5));
        t1 = (t1 & 0xFFFF00FFFFFFFFFFULL) | (tmp << 40);
    }

    if (GET_LMASK64(t0) >= 6) {
        tmp = ldfun(GET_OFFSET(t0, -6));
        t1 = (t1 & 0xFF00FFFFFFFFFFFFULL) | (tmp << 48);
    }

    if (GET_LMASK64(t0) == 7) {
        tmp = ldfun(GET_OFFSET(t0, -7));
        t1 = (t1 & 0x00FFFFFFFFFFFFFFULL) | (tmp << 56);
    }

    return t1;
}

void helper_sdl(target_ulong t0, target_ulong t1, int mem_idx)
{
#ifdef CONFIG_USER_ONLY
#define stfun stb_raw
#else
    void (*stfun)(target_ulong, int);

    switch (mem_idx)
    {
    case 0: stfun = stb_kernel; break;
    case 1: stfun = stb_super; break;
    default:
    case 2: stfun = stb_user; break;
    }
#endif
    stfun(t0, (uint8_t)(t1 >> 56));

    if (GET_LMASK64(t0) <= 6)
        stfun(GET_OFFSET(t0, 1), (uint8_t)(t1 >> 48));

    if (GET_LMASK64(t0) <= 5)
        stfun(GET_OFFSET(t0, 2), (uint8_t)(t1 >> 40));

    if (GET_LMASK64(t0) <= 4)
        stfun(GET_OFFSET(t0, 3), (uint8_t)(t1 >> 32));

    if (GET_LMASK64(t0) <= 3)
        stfun(GET_OFFSET(t0, 4), (uint8_t)(t1 >> 24));

    if (GET_LMASK64(t0) <= 2)
        stfun(GET_OFFSET(t0, 5), (uint8_t)(t1 >> 16));

    if (GET_LMASK64(t0) <= 1)
        stfun(GET_OFFSET(t0, 6), (uint8_t)(t1 >> 8));

    if (GET_LMASK64(t0) <= 0)
        stfun(GET_OFFSET(t0, 7), (uint8_t)t1);
}

void helper_sdr(target_ulong t0, target_ulong t1, int mem_idx)
{
#ifdef CONFIG_USER_ONLY
#define stfun stb_raw
#else
    void (*stfun)(target_ulong, int);

    switch (mem_idx)
    {
    case 0: stfun = stb_kernel; break;
    case 1: stfun = stb_super; break;
     default:
    case 2: stfun = stb_user; break;
    }
#endif
    stfun(t0, (uint8_t)t1);

    if (GET_LMASK64(t0) >= 1)
        stfun(GET_OFFSET(t0, -1), (uint8_t)(t1 >> 8));

    if (GET_LMASK64(t0) >= 2)
        stfun(GET_OFFSET(t0, -2), (uint8_t)(t1 >> 16));

    if (GET_LMASK64(t0) >= 3)
        stfun(GET_OFFSET(t0, -3), (uint8_t)(t1 >> 24));

    if (GET_LMASK64(t0) >= 4)
        stfun(GET_OFFSET(t0, -4), (uint8_t)(t1 >> 32));

    if (GET_LMASK64(t0) >= 5)
        stfun(GET_OFFSET(t0, -5), (uint8_t)(t1 >> 40));

    if (GET_LMASK64(t0) >= 6)
        stfun(GET_OFFSET(t0, -6), (uint8_t)(t1 >> 48));

    if (GET_LMASK64(t0) == 7)
        stfun(GET_OFFSET(t0, -7), (uint8_t)(t1 >> 56));
}
#endif /* TARGET_MIPS64 */

#ifndef CONFIG_USER_ONLY
/* CP0 helpers */
target_ulong helper_mfc0_mvpcontrol (void)
{
    return env->mvp->CP0_MVPControl;
}

target_ulong helper_mfc0_mvpconf0 (void)
{
    return env->mvp->CP0_MVPConf0;
}

target_ulong helper_mfc0_mvpconf1 (void)
{
    return env->mvp->CP0_MVPConf1;
}

target_ulong helper_mfc0_random (void)
{
    return (int32_t)cpu_mips_get_random(env);
}

target_ulong helper_mfc0_tcstatus (void)
{
    return env->active_tc.CP0_TCStatus;
}

target_ulong helper_mftc0_tcstatus(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.CP0_TCStatus;
    else
        return env->tcs[other_tc].CP0_TCStatus;
}

target_ulong helper_mfc0_tcbind (void)
{
    return env->active_tc.CP0_TCBind;
}

target_ulong helper_mftc0_tcbind(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.CP0_TCBind;
    else
        return env->tcs[other_tc].CP0_TCBind;
}

target_ulong helper_mfc0_tcrestart (void)
{
    return env->active_tc.PC;
}

target_ulong helper_mftc0_tcrestart(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.PC;
    else
        return env->tcs[other_tc].PC;
}

target_ulong helper_mfc0_tchalt (void)
{
    return env->active_tc.CP0_TCHalt;
}

target_ulong helper_mftc0_tchalt(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.CP0_TCHalt;
    else
        return env->tcs[other_tc].CP0_TCHalt;
}

target_ulong helper_mfc0_tccontext (void)
{
    return env->active_tc.CP0_TCContext;
}

target_ulong helper_mftc0_tccontext(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.CP0_TCContext;
    else
        return env->tcs[other_tc].CP0_TCContext;
}

target_ulong helper_mfc0_tcschedule (void)
{
    return env->active_tc.CP0_TCSchedule;
}

target_ulong helper_mftc0_tcschedule(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.CP0_TCSchedule;
    else
        return env->tcs[other_tc].CP0_TCSchedule;
}

target_ulong helper_mfc0_tcschefback (void)
{
    return env->active_tc.CP0_TCScheFBack;
}

target_ulong helper_mftc0_tcschefback(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.CP0_TCScheFBack;
    else
        return env->tcs[other_tc].CP0_TCScheFBack;
}

target_ulong helper_mfc0_count (void)
{
    return (int32_t)cpu_mips_get_count(env);
}

target_ulong helper_mftc0_entryhi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tcstatus;

    if (other_tc == env->current_tc)
        tcstatus = env->active_tc.CP0_TCStatus;
    else
        tcstatus = env->tcs[other_tc].CP0_TCStatus;

    return (env->CP0_EntryHi & ~0xff) | (tcstatus & 0xff);
}

target_ulong helper_mftc0_status(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    target_ulong t0;
    int32_t tcstatus;

    if (other_tc == env->current_tc)
        tcstatus = env->active_tc.CP0_TCStatus;
    else
        tcstatus = env->tcs[other_tc].CP0_TCStatus;

    t0 = env->CP0_Status & ~0xf1000018;
    t0 |= tcstatus & (0xf << CP0TCSt_TCU0);
    t0 |= (tcstatus & (1 << CP0TCSt_TMX)) >> (CP0TCSt_TMX - CP0St_MX);
    t0 |= (tcstatus & (0x3 << CP0TCSt_TKSU)) >> (CP0TCSt_TKSU - CP0St_KSU);

    return t0;
}

target_ulong helper_mfc0_lladdr (void)
{
    return (int32_t)env->CP0_LLAddr >> 4;
}

target_ulong helper_mfc0_watchlo (uint32_t sel)
{
    return (int32_t)env->CP0_WatchLo[sel];
}

target_ulong helper_mfc0_watchhi (uint32_t sel)
{
    return env->CP0_WatchHi[sel];
}

target_ulong helper_mfc0_debug (void)
{
    target_ulong t0 = env->CP0_Debug;
    if (env->hflags & MIPS_HFLAG_DM)
        t0 |= 1 << CP0DB_DM;

    return t0;
}

target_ulong helper_mftc0_debug(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tcstatus;

    if (other_tc == env->current_tc)
        tcstatus = env->active_tc.CP0_Debug_tcstatus;
    else
        tcstatus = env->tcs[other_tc].CP0_Debug_tcstatus;

    /* XXX: Might be wrong, check with EJTAG spec. */
    return (env->CP0_Debug & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
            (tcstatus & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

#if defined(TARGET_MIPS64)
target_ulong helper_dmfc0_tcrestart (void)
{
    return env->active_tc.PC;
}

target_ulong helper_dmfc0_tchalt (void)
{
    return env->active_tc.CP0_TCHalt;
}

target_ulong helper_dmfc0_tccontext (void)
{
    return env->active_tc.CP0_TCContext;
}

target_ulong helper_dmfc0_tcschedule (void)
{
    return env->active_tc.CP0_TCSchedule;
}

target_ulong helper_dmfc0_tcschefback (void)
{
    return env->active_tc.CP0_TCScheFBack;
}

target_ulong helper_dmfc0_lladdr (void)
{
    return env->CP0_LLAddr >> 4;
}

target_ulong helper_dmfc0_watchlo (uint32_t sel)
{
    return env->CP0_WatchLo[sel];
}
#endif /* TARGET_MIPS64 */

void helper_mtc0_index (target_ulong t0)
{
    int num = 1;
    unsigned int tmp = env->tlb->nb_tlb;

    do {
        tmp >>= 1;
        num <<= 1;
    } while (tmp);
    env->CP0_Index = (env->CP0_Index & 0x80000000) | (t0 & (num - 1));
}

void helper_mtc0_mvpcontrol (target_ulong t0)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP))
        mask |= (1 << CP0MVPCo_CPA) | (1 << CP0MVPCo_VPC) |
                (1 << CP0MVPCo_EVP);
    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0MVPCo_STLB);
    newval = (env->mvp->CP0_MVPControl & ~mask) | (t0 & mask);

    // TODO: Enable/disable shared TLB, enable/disable VPEs.

    env->mvp->CP0_MVPControl = newval;
}

void helper_mtc0_vpecontrol (target_ulong t0)
{
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (env->CP0_VPEControl & ~mask) | (t0 & mask);

    /* Yield scheduler intercept not implemented. */
    /* Gating storage scheduler intercept not implemented. */

    // TODO: Enable/disable TCs.

    env->CP0_VPEControl = newval;
}

void helper_mtc0_vpeconf0 (target_ulong t0)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) {
        if (env->CP0_VPEConf0 & (1 << CP0VPEC0_VPA))
            mask |= (0xff << CP0VPEC0_XTC);
        mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    }
    newval = (env->CP0_VPEConf0 & ~mask) | (t0 & mask);

    // TODO: TC exclusive handling due to ERL/EXL.

    env->CP0_VPEConf0 = newval;
}

void helper_mtc0_vpeconf1 (target_ulong t0)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (0xff << CP0VPEC1_NCX) | (0xff << CP0VPEC1_NCP2) |
                (0xff << CP0VPEC1_NCP1);
    newval = (env->CP0_VPEConf1 & ~mask) | (t0 & mask);

    /* UDI not implemented. */
    /* CP2 not implemented. */

    // TODO: Handle FPU (CP1) binding.

    env->CP0_VPEConf1 = newval;
}

void helper_mtc0_yqmask (target_ulong t0)
{
    /* Yield qualifier inputs not implemented. */
    env->CP0_YQMask = 0x00000000;
}

void helper_mtc0_vpeopt (target_ulong t0)
{
    env->CP0_VPEOpt = t0 & 0x0000ffff;
}

void helper_mtc0_entrylo0 (target_ulong t0)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo0 = t0 & 0x3FFFFFFF;
}

void helper_mtc0_tcstatus (target_ulong t0)
{
    uint32_t mask = env->CP0_TCStatus_rw_bitmask;
    uint32_t newval;

    newval = (env->active_tc.CP0_TCStatus & ~mask) | (t0 & mask);

    // TODO: Sync with CP0_Status.

    env->active_tc.CP0_TCStatus = newval;
}

void helper_mttc0_tcstatus (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    // TODO: Sync with CP0_Status.

    if (other_tc == env->current_tc)
        env->active_tc.CP0_TCStatus = t0;
    else
        env->tcs[other_tc].CP0_TCStatus = t0;
}

void helper_mtc0_tcbind (target_ulong t0)
{
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    newval = (env->active_tc.CP0_TCBind & ~mask) | (t0 & mask);
    env->active_tc.CP0_TCBind = newval;
}

void helper_mttc0_tcbind (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    if (other_tc == env->current_tc) {
        newval = (env->active_tc.CP0_TCBind & ~mask) | (t0 & mask);
        env->active_tc.CP0_TCBind = newval;
    } else {
        newval = (env->tcs[other_tc].CP0_TCBind & ~mask) | (t0 & mask);
        env->tcs[other_tc].CP0_TCBind = newval;
    }
}

void helper_mtc0_tcrestart (target_ulong t0)
{
    env->active_tc.PC = t0;
    env->active_tc.CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
    env->CP0_LLAddr = 0ULL;
    /* MIPS16 not implemented. */
}

void helper_mttc0_tcrestart (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc) {
        env->active_tc.PC = t0;
        env->active_tc.CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
        env->CP0_LLAddr = 0ULL;
        /* MIPS16 not implemented. */
    } else {
        env->tcs[other_tc].PC = t0;
        env->tcs[other_tc].CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
        env->CP0_LLAddr = 0ULL;
        /* MIPS16 not implemented. */
    }
}

void helper_mtc0_tchalt (target_ulong t0)
{
    env->active_tc.CP0_TCHalt = t0 & 0x1;

    // TODO: Halt TC / Restart (if allocated+active) TC.
}

void helper_mttc0_tchalt (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    // TODO: Halt TC / Restart (if allocated+active) TC.

    if (other_tc == env->current_tc)
        env->active_tc.CP0_TCHalt = t0;
    else
        env->tcs[other_tc].CP0_TCHalt = t0;
}

void helper_mtc0_tccontext (target_ulong t0)
{
    env->active_tc.CP0_TCContext = t0;
}

void helper_mttc0_tccontext (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.CP0_TCContext = t0;
    else
        env->tcs[other_tc].CP0_TCContext = t0;
}

void helper_mtc0_tcschedule (target_ulong t0)
{
    env->active_tc.CP0_TCSchedule = t0;
}

void helper_mttc0_tcschedule (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.CP0_TCSchedule = t0;
    else
        env->tcs[other_tc].CP0_TCSchedule = t0;
}

void helper_mtc0_tcschefback (target_ulong t0)
{
    env->active_tc.CP0_TCScheFBack = t0;
}

void helper_mttc0_tcschefback (target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.CP0_TCScheFBack = t0;
    else
        env->tcs[other_tc].CP0_TCScheFBack = t0;
}

void helper_mtc0_entrylo1 (target_ulong t0)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo1 = t0 & 0x3FFFFFFF;
}

void helper_mtc0_context (target_ulong t0)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (t0 & ~0x007FFFFF);
}

void helper_mtc0_pagemask (target_ulong t0)
{
    /* 1k pages not implemented */
    env->CP0_PageMask = t0 & (0x1FFFFFFF & (TARGET_PAGE_MASK << 1));
}

void helper_mtc0_pagegrain (target_ulong t0)
{
    /* SmartMIPS not implemented */
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = 0;
}

void helper_mtc0_wired (target_ulong t0)
{
    env->CP0_Wired = t0 % env->tlb->nb_tlb;
}

void helper_mtc0_srsconf0 (target_ulong t0)
{
    env->CP0_SRSConf0 |= t0 & env->CP0_SRSConf0_rw_bitmask;
}

void helper_mtc0_srsconf1 (target_ulong t0)
{
    env->CP0_SRSConf1 |= t0 & env->CP0_SRSConf1_rw_bitmask;
}

void helper_mtc0_srsconf2 (target_ulong t0)
{
    env->CP0_SRSConf2 |= t0 & env->CP0_SRSConf2_rw_bitmask;
}

void helper_mtc0_srsconf3 (target_ulong t0)
{
    env->CP0_SRSConf3 |= t0 & env->CP0_SRSConf3_rw_bitmask;
}

void helper_mtc0_srsconf4 (target_ulong t0)
{
    env->CP0_SRSConf4 |= t0 & env->CP0_SRSConf4_rw_bitmask;
}

void helper_mtc0_hwrena (target_ulong t0)
{
    env->CP0_HWREna = t0 & 0x0000000F;
}

void helper_mtc0_count (target_ulong t0)
{
    cpu_mips_store_count(env, t0);
}

void helper_mtc0_entryhi (target_ulong t0)
{
    target_ulong old, val;

    /* 1k pages not implemented */
    val = t0 & ((TARGET_PAGE_MASK << 1) | 0xFF);
#if defined(TARGET_MIPS64)
    val &= env->SEGMask;
#endif
    old = env->CP0_EntryHi;
    env->CP0_EntryHi = val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        uint32_t tcst = env->active_tc.CP0_TCStatus & ~0xff;
        env->active_tc.CP0_TCStatus = tcst | (val & 0xff);
    }
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & 0xFF) != (val & 0xFF))
        cpu_mips_tlb_flush(env, 1);
}

void helper_mttc0_entryhi(target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tcstatus;

    env->CP0_EntryHi = (env->CP0_EntryHi & 0xff) | (t0 & ~0xff);
    if (other_tc == env->current_tc) {
        tcstatus = (env->active_tc.CP0_TCStatus & ~0xff) | (t0 & 0xff);
        env->active_tc.CP0_TCStatus = tcstatus;
    } else {
        tcstatus = (env->tcs[other_tc].CP0_TCStatus & ~0xff) | (t0 & 0xff);
        env->tcs[other_tc].CP0_TCStatus = tcstatus;
    }
}

void helper_mtc0_compare (target_ulong t0)
{
    cpu_mips_store_compare(env, t0);
}

void helper_mtc0_status (target_ulong t0)
{
    uint32_t val, old;
    uint32_t mask = env->CP0_Status_rw_bitmask;

    val = t0 & mask;
    old = env->CP0_Status;
    env->CP0_Status = (env->CP0_Status & ~mask) | val;
    compute_hflags(env);
    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("Status %08x (%08x) => %08x (%08x) Cause %08x",
                old, old & env->CP0_Cause & CP0Ca_IP_mask,
                val, val & env->CP0_Cause & CP0Ca_IP_mask,
                env->CP0_Cause);
        switch (env->hflags & MIPS_HFLAG_KSU) {
        case MIPS_HFLAG_UM: qemu_log(", UM\n"); break;
        case MIPS_HFLAG_SM: qemu_log(", SM\n"); break;
        case MIPS_HFLAG_KM: qemu_log("\n"); break;
        default: cpu_abort(env, "Invalid MMU mode!\n"); break;
	}
    }
    cpu_mips_update_irq(env);
}

void helper_mttc0_status(target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tcstatus = env->tcs[other_tc].CP0_TCStatus;

    env->CP0_Status = t0 & ~0xf1000018;
    tcstatus = (tcstatus & ~(0xf << CP0TCSt_TCU0)) | (t0 & (0xf << CP0St_CU0));
    tcstatus = (tcstatus & ~(1 << CP0TCSt_TMX)) | ((t0 & (1 << CP0St_MX)) << (CP0TCSt_TMX - CP0St_MX));
    tcstatus = (tcstatus & ~(0x3 << CP0TCSt_TKSU)) | ((t0 & (0x3 << CP0St_KSU)) << (CP0TCSt_TKSU - CP0St_KSU));
    if (other_tc == env->current_tc)
        env->active_tc.CP0_TCStatus = tcstatus;
    else
        env->tcs[other_tc].CP0_TCStatus = tcstatus;
}

void helper_mtc0_intctl (target_ulong t0)
{
    /* vectored interrupts not implemented, no performance counters. */
    env->CP0_IntCtl = (env->CP0_IntCtl & ~0x000002e0) | (t0 & 0x000002e0);
}

void helper_mtc0_srsctl (target_ulong t0)
{
    uint32_t mask = (0xf << CP0SRSCtl_ESS) | (0xf << CP0SRSCtl_PSS);
    env->CP0_SRSCtl = (env->CP0_SRSCtl & ~mask) | (t0 & mask);
}

void helper_mtc0_cause (target_ulong t0)
{
    uint32_t mask = 0x00C00300;
    uint32_t old = env->CP0_Cause;

    if (env->insn_flags & ISA_MIPS32R2)
        mask |= 1 << CP0Ca_DC;

    env->CP0_Cause = (env->CP0_Cause & ~mask) | (t0 & mask);

    if ((old ^ env->CP0_Cause) & (1 << CP0Ca_DC)) {
        if (env->CP0_Cause & (1 << CP0Ca_DC))
            cpu_mips_stop_count(env);
        else
            cpu_mips_start_count(env);
    }

    /* Handle the software interrupt as an hardware one, as they
       are very similar */
    if (t0 & CP0Ca_IP_mask) {
        cpu_mips_update_irq(env);
    }
}

void helper_mtc0_ebase (target_ulong t0)
{
    /* vectored interrupts not implemented */
    /* Multi-CPU not implemented */
    env->CP0_EBase = 0x80000000 | (t0 & 0x3FFFF000);
}

void helper_mtc0_config0 (target_ulong t0)
{
    env->CP0_Config0 = (env->CP0_Config0 & 0x81FFFFF8) | (t0 & 0x00000007);
}

void helper_mtc0_config2 (target_ulong t0)
{
    /* tertiary/secondary caches not implemented */
    env->CP0_Config2 = (env->CP0_Config2 & 0x8FFF0FFF);
}

void helper_mtc0_watchlo (target_ulong t0, uint32_t sel)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo[sel] = (t0 & ~0x7);
}

void helper_mtc0_watchhi (target_ulong t0, uint32_t sel)
{
    env->CP0_WatchHi[sel] = (t0 & 0x40FF0FF8);
    env->CP0_WatchHi[sel] &= ~(env->CP0_WatchHi[sel] & t0 & 0x7);
}

void helper_mtc0_xcontext (target_ulong t0)
{
    target_ulong mask = (1ULL << (env->SEGBITS - 7)) - 1;
    env->CP0_XContext = (env->CP0_XContext & mask) | (t0 & ~mask);
}

void helper_mtc0_framemask (target_ulong t0)
{
    env->CP0_Framemask = t0; /* XXX */
}

void helper_mtc0_debug (target_ulong t0)
{
    env->CP0_Debug = (env->CP0_Debug & 0x8C03FC1F) | (t0 & 0x13300120);
    if (t0 & (1 << CP0DB_DM))
        env->hflags |= MIPS_HFLAG_DM;
    else
        env->hflags &= ~MIPS_HFLAG_DM;
}

void helper_mttc0_debug(target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t val = t0 & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt));

    /* XXX: Might be wrong, check with EJTAG spec. */
    if (other_tc == env->current_tc)
        env->active_tc.CP0_Debug_tcstatus = val;
    else
        env->tcs[other_tc].CP0_Debug_tcstatus = val;
    env->CP0_Debug = (env->CP0_Debug & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
                     (t0 & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

void helper_mtc0_performance0 (target_ulong t0)
{
    env->CP0_Performance0 = t0 & 0x000007ff;
}

void helper_mtc0_taglo (target_ulong t0)
{
    env->CP0_TagLo = t0 & 0xFFFFFCF6;
}

void helper_mtc0_datalo (target_ulong t0)
{
    env->CP0_DataLo = t0; /* XXX */
}

void helper_mtc0_taghi (target_ulong t0)
{
    env->CP0_TagHi = t0; /* XXX */
}

void helper_mtc0_datahi (target_ulong t0)
{
    env->CP0_DataHi = t0; /* XXX */
}

/* MIPS MT functions */
target_ulong helper_mftgpr(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.gpr[sel];
    else
        return env->tcs[other_tc].gpr[sel];
}

target_ulong helper_mftlo(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.LO[sel];
    else
        return env->tcs[other_tc].LO[sel];
}

target_ulong helper_mfthi(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.HI[sel];
    else
        return env->tcs[other_tc].HI[sel];
}

target_ulong helper_mftacx(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.ACX[sel];
    else
        return env->tcs[other_tc].ACX[sel];
}

target_ulong helper_mftdsp(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        return env->active_tc.DSPControl;
    else
        return env->tcs[other_tc].DSPControl;
}

void helper_mttgpr(target_ulong t0, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.gpr[sel] = t0;
    else
        env->tcs[other_tc].gpr[sel] = t0;
}

void helper_mttlo(target_ulong t0, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.LO[sel] = t0;
    else
        env->tcs[other_tc].LO[sel] = t0;
}

void helper_mtthi(target_ulong t0, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.HI[sel] = t0;
    else
        env->tcs[other_tc].HI[sel] = t0;
}

void helper_mttacx(target_ulong t0, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.ACX[sel] = t0;
    else
        env->tcs[other_tc].ACX[sel] = t0;
}

void helper_mttdsp(target_ulong t0)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    if (other_tc == env->current_tc)
        env->active_tc.DSPControl = t0;
    else
        env->tcs[other_tc].DSPControl = t0;
}

/* MIPS MT functions */
target_ulong helper_dmt(target_ulong t0)
{
    // TODO
    t0 = 0;
    // rt = t0

    return t0;
}

target_ulong helper_emt(target_ulong t0)
{
    // TODO
    t0 = 0;
    // rt = t0

    return t0;
}

target_ulong helper_dvpe(target_ulong t0)
{
    // TODO
    t0 = 0;
    // rt = t0

    return t0;
}

target_ulong helper_evpe(target_ulong t0)
{
    // TODO
    t0 = 0;
    // rt = t0

    return t0;
}
#endif /* !CONFIG_USER_ONLY */

void helper_fork(target_ulong t0, target_ulong t1)
{
    // t0 = rt, t1 = rs
    t0 = 0;
    // TODO: store to TC register
}

target_ulong helper_yield(target_ulong t0)
{
    if (t0 < 0) {
        /* No scheduling policy implemented. */
        if (t0 != -2) {
            if (env->CP0_VPEControl & (1 << CP0VPECo_YSI) &&
                env->active_tc.CP0_TCStatus & (1 << CP0TCSt_DT)) {
                env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
                env->CP0_VPEControl |= 4 << CP0VPECo_EXCPT;
                helper_raise_exception(EXCP_THREAD);
            }
        }
    } else if (t0 == 0) {
        if (0 /* TODO: TC underflow */) {
            env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
            helper_raise_exception(EXCP_THREAD);
        } else {
            // TODO: Deallocate TC
        }
    } else if (t0 > 0) {
        /* Yield qualifier inputs not implemented. */
        env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
        env->CP0_VPEControl |= 2 << CP0VPECo_EXCPT;
        helper_raise_exception(EXCP_THREAD);
    }
    return env->CP0_YQMask;
}

#ifndef CONFIG_USER_ONLY
/* TLB management */
void cpu_mips_tlb_flush (CPUState *env, int flush_global)
{
    /* Flush qemu's TLB and discard all shadowed entries.  */
    tlb_flush (env, flush_global);
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
}

static void r4k_mips_tlb_flush_extra (CPUState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb->tlb_in_use > first) {
        r4k_invalidate_tlb(env, --env->tlb->tlb_in_use, 0);
    }
}

static void r4k_fill_tlb (int idx)
{
    r4k_tlb_t *tlb;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    tlb->VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    tlb->VPN &= env->SEGMask;
#endif
    tlb->ASID = env->CP0_EntryHi & 0xFF;
    tlb->PageMask = env->CP0_PageMask;
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V0 = (env->CP0_EntryLo0 & 2) != 0;
    tlb->D0 = (env->CP0_EntryLo0 & 4) != 0;
    tlb->C0 = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->PFN[0] = (env->CP0_EntryLo0 >> 6) << 12;
    tlb->V1 = (env->CP0_EntryLo1 & 2) != 0;
    tlb->D1 = (env->CP0_EntryLo1 & 4) != 0;
    tlb->C1 = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->PFN[1] = (env->CP0_EntryLo1 >> 6) << 12;
}

void r4k_helper_tlbwi (void)
{
    int idx;

    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;

    /* Discard cached TLB entries.  We could avoid doing this if the
       tlbwi is just upgrading access permissions on the current entry;
       that might be a further win.  */
    r4k_mips_tlb_flush_extra (env, env->tlb->nb_tlb);

    r4k_invalidate_tlb(env, idx, 0);
    r4k_fill_tlb(idx);
}

void r4k_helper_tlbwr (void)
{
    int r = cpu_mips_get_random(env);

    r4k_invalidate_tlb(env, r, 1);
    r4k_fill_tlb(r);
}

void r4k_helper_tlbp (void)
{
    r4k_tlb_t *tlb;
    target_ulong mask;
    target_ulong tag;
    target_ulong VPN;
    uint8_t ASID;
    int i;

    ASID = env->CP0_EntryHi & 0xFF;
    for (i = 0; i < env->tlb->nb_tlb; i++) {
        tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        tag = env->CP0_EntryHi & ~mask;
        VPN = tlb->VPN & ~mask;
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
            /* TLB match */
            env->CP0_Index = i;
            break;
        }
    }
    if (i == env->tlb->nb_tlb) {
        /* No match.  Discard any shadow entries, if any of them match.  */
        for (i = env->tlb->nb_tlb; i < env->tlb->tlb_in_use; i++) {
            tlb = &env->tlb->mmu.r4k.tlb[i];
            /* 1k pages are not supported. */
            mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
            tag = env->CP0_EntryHi & ~mask;
            VPN = tlb->VPN & ~mask;
            /* Check ASID, virtual page number & size */
            if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
                r4k_mips_tlb_flush_extra (env, i);
                break;
            }
        }

        env->CP0_Index |= 0x80000000;
    }
}

void r4k_helper_tlbr (void)
{
    r4k_tlb_t *tlb;
    uint8_t ASID;
    int idx;

    ASID = env->CP0_EntryHi & 0xFF;
    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID)
        cpu_mips_tlb_flush (env, 1);

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    env->CP0_PageMask = tlb->PageMask;
    env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
}

void helper_tlbwi(void)
{
    env->tlb->helper_tlbwi();
}

void helper_tlbwr(void)
{
    env->tlb->helper_tlbwr();
}

void helper_tlbp(void)
{
    env->tlb->helper_tlbp();
}

void helper_tlbr(void)
{
    env->tlb->helper_tlbr();
}

/* Specials */
target_ulong helper_di (void)
{
    target_ulong t0 = env->CP0_Status;

    env->CP0_Status = t0 & ~(1 << CP0St_IE);
    cpu_mips_update_irq(env);

    return t0;
}

target_ulong helper_ei (void)
{
    target_ulong t0 = env->CP0_Status;

    env->CP0_Status = t0 | (1 << CP0St_IE);
    cpu_mips_update_irq(env);

    return t0;
}

static void debug_pre_eret (void)
{
    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("ERET: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
                env->active_tc.PC, env->CP0_EPC);
        if (env->CP0_Status & (1 << CP0St_ERL))
            qemu_log(" ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
        if (env->hflags & MIPS_HFLAG_DM)
            qemu_log(" DEPC " TARGET_FMT_lx, env->CP0_DEPC);
        qemu_log("\n");
    }
}

static void debug_post_eret (void)
{
    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("  =>  PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
                env->active_tc.PC, env->CP0_EPC);
        if (env->CP0_Status & (1 << CP0St_ERL))
            qemu_log(" ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
        if (env->hflags & MIPS_HFLAG_DM)
            qemu_log(" DEPC " TARGET_FMT_lx, env->CP0_DEPC);
        switch (env->hflags & MIPS_HFLAG_KSU) {
        case MIPS_HFLAG_UM: qemu_log(", UM\n"); break;
        case MIPS_HFLAG_SM: qemu_log(", SM\n"); break;
        case MIPS_HFLAG_KM: qemu_log("\n"); break;
        default: cpu_abort(env, "Invalid MMU mode!\n"); break;
        }
    }
}

void helper_eret (void)
{
    debug_pre_eret();
    if (env->CP0_Status & (1 << CP0St_ERL)) {
        env->active_tc.PC = env->CP0_ErrorEPC;
        env->CP0_Status &= ~(1 << CP0St_ERL);
    } else {
        env->active_tc.PC = env->CP0_EPC;
        env->CP0_Status &= ~(1 << CP0St_EXL);
    }
    compute_hflags(env);
    debug_post_eret();
    env->CP0_LLAddr = 1;
}

void helper_deret (void)
{
    debug_pre_eret();
    env->active_tc.PC = env->CP0_DEPC;
    env->hflags &= MIPS_HFLAG_DM;
    compute_hflags(env);
    debug_post_eret();
    env->CP0_LLAddr = 1;
}
#endif /* !CONFIG_USER_ONLY */

target_ulong helper_rdhwr_cpunum(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 0)))
        return env->CP0_EBase & 0x3ff;
    else
        helper_raise_exception(EXCP_RI);

    return 0;
}

target_ulong helper_rdhwr_synci_step(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 1)))
        return env->SYNCI_Step;
    else
        helper_raise_exception(EXCP_RI);

    return 0;
}

target_ulong helper_rdhwr_cc(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 2)))
        return env->CP0_Count;
    else
        helper_raise_exception(EXCP_RI);

    return 0;
}

target_ulong helper_rdhwr_ccres(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 3)))
        return env->CCRes;
    else
        helper_raise_exception(EXCP_RI);

    return 0;
}

void helper_pmon (int function)
{
    function /= 2;
    switch (function) {
    case 2: /* TODO: char inbyte(int waitflag); */
        if (env->active_tc.gpr[4] == 0)
            env->active_tc.gpr[2] = -1;
        /* Fall through */
    case 11: /* TODO: char inbyte (void); */
        env->active_tc.gpr[2] = -1;
        break;
    case 3:
    case 12:
        printf("%c", (char)(env->active_tc.gpr[4] & 0xFF));
        break;
    case 17:
        break;
    case 158:
        {
            unsigned char *fmt = (void *)(unsigned long)env->active_tc.gpr[4];
            printf("%s", fmt);
        }
        break;
    }
}

void helper_wait (void)
{
    env->halted = 1;
    helper_raise_exception(EXCP_HLT);
}

#if !defined(CONFIG_USER_ONLY)

static void do_unaligned_access (target_ulong addr, int is_write, int is_user, void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

static void do_unaligned_access (target_ulong addr, int is_write, int is_user, void *retaddr)
{
    env->CP0_BadVAddr = addr;
    do_restore_state (retaddr);
    helper_raise_exception ((is_write == 1) ? EXCP_AdES : EXCP_AdEL);
}

void tlb_fill (target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_mips_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        helper_raise_exception_err(env->exception_index, env->error_code);
    }
    env = saved_env;
}

void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int unused, int size)
{
    if (is_exec)
        helper_raise_exception(EXCP_IBE);
    else
        helper_raise_exception(EXCP_DBE);
}
#endif /* !CONFIG_USER_ONLY */

/* Complex FPU operations which may need stack space. */

#define FLOAT_ONE32 make_float32(0x3f8 << 20)
#define FLOAT_ONE64 make_float64(0x3ffULL << 52)
#define FLOAT_TWO32 make_float32(1 << 30)
#define FLOAT_TWO64 make_float64(1ULL << 62)
#define FLOAT_QNAN32 0x7fbfffff
#define FLOAT_QNAN64 0x7ff7ffffffffffffULL
#define FLOAT_SNAN32 0x7fffffff
#define FLOAT_SNAN64 0x7fffffffffffffffULL

/* convert MIPS rounding mode in FCR31 to IEEE library */
unsigned int ieee_rm[] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

#define RESTORE_ROUNDING_MODE \
    set_float_rounding_mode(ieee_rm[env->active_fpu.fcr31 & 3], &env->active_fpu.fp_status)

target_ulong helper_cfc1 (uint32_t reg)
{
    target_ulong t0;

    switch (reg) {
    case 0:
        t0 = (int32_t)env->active_fpu.fcr0;
        break;
    case 25:
        t0 = ((env->active_fpu.fcr31 >> 24) & 0xfe) | ((env->active_fpu.fcr31 >> 23) & 0x1);
        break;
    case 26:
        t0 = env->active_fpu.fcr31 & 0x0003f07c;
        break;
    case 28:
        t0 = (env->active_fpu.fcr31 & 0x00000f83) | ((env->active_fpu.fcr31 >> 22) & 0x4);
        break;
    default:
        t0 = (int32_t)env->active_fpu.fcr31;
        break;
    }

    return t0;
}

void helper_ctc1 (target_ulong t0, uint32_t reg)
{
    switch(reg) {
    case 25:
        if (t0 & 0xffffff00)
            return;
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0x017fffff) | ((t0 & 0xfe) << 24) |
                     ((t0 & 0x1) << 23);
        break;
    case 26:
        if (t0 & 0x007c0000)
            return;
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfffc0f83) | (t0 & 0x0003f07c);
        break;
    case 28:
        if (t0 & 0x007c0000)
            return;
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfefff07c) | (t0 & 0x00000f83) |
                     ((t0 & 0x4) << 22);
        break;
    case 31:
        if (t0 & 0x007c0000)
            return;
        env->active_fpu.fcr31 = t0;
        break;
    default:
        return;
    }
    /* set rounding mode */
    RESTORE_ROUNDING_MODE;
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    if ((GET_FP_ENABLE(env->active_fpu.fcr31) | 0x20) & GET_FP_CAUSE(env->active_fpu.fcr31))
        helper_raise_exception(EXCP_FPE);
}

static inline char ieee_ex_to_mips(char xcpt)
{
    return (xcpt & float_flag_inexact) >> 5 |
           (xcpt & float_flag_underflow) >> 3 |
           (xcpt & float_flag_overflow) >> 1 |
           (xcpt & float_flag_divbyzero) << 1 |
           (xcpt & float_flag_invalid) << 4;
}

static inline char mips_ex_to_ieee(char xcpt)
{
    return (xcpt & FP_INEXACT) << 5 |
           (xcpt & FP_UNDERFLOW) << 3 |
           (xcpt & FP_OVERFLOW) << 1 |
           (xcpt & FP_DIV0) >> 1 |
           (xcpt & FP_INVALID) >> 4;
}

static inline void update_fcr31(void)
{
    int tmp = ieee_ex_to_mips(get_float_exception_flags(&env->active_fpu.fp_status));

    SET_FP_CAUSE(env->active_fpu.fcr31, tmp);
    if (GET_FP_ENABLE(env->active_fpu.fcr31) & tmp)
        helper_raise_exception(EXCP_FPE);
    else
        UPDATE_FP_FLAGS(env->active_fpu.fcr31, tmp);
}

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix, 32bit integer "w", 64bit integer "l", paired single "ps",
   paired single lower "pl", paired single upper "pu".  */

/* unary operations, modifying fp status  */
uint64_t helper_float_sqrt_d(uint64_t fdt0)
{
    return float64_sqrt(fdt0, &env->active_fpu.fp_status);
}

uint32_t helper_float_sqrt_s(uint32_t fst0)
{
    return float32_sqrt(fst0, &env->active_fpu.fp_status);
}

uint64_t helper_float_cvtd_s(uint32_t fst0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float32_to_float64(fst0, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint64_t helper_float_cvtd_w(uint32_t wt0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = int32_to_float64(wt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint64_t helper_float_cvtd_l(uint64_t dt0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = int64_to_float64(dt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint64_t helper_float_cvtl_d(uint64_t fdt0)
{
    uint64_t dt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint64_t helper_float_cvtl_s(uint32_t fst0)
{
    uint64_t dt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint64_t helper_float_cvtps_pw(uint64_t dt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = int32_to_float32(dt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = int32_to_float32(dt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_cvtpw_ps(uint64_t fdt0)
{
    uint32_t wt2;
    uint32_t wth2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    wth2 = float32_to_int32(fdt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID)) {
        wt2 = FLOAT_SNAN32;
        wth2 = FLOAT_SNAN32;
    }
    return ((uint64_t)wth2 << 32) | wt2;
}

uint32_t helper_float_cvts_d(uint64_t fdt0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float64_to_float32(fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint32_t helper_float_cvts_w(uint32_t wt0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = int32_to_float32(wt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint32_t helper_float_cvts_l(uint64_t dt0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = int64_to_float32(dt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint32_t helper_float_cvts_pl(uint32_t wt0)
{
    uint32_t wt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wt2 = wt0;
    update_fcr31();
    return wt2;
}

uint32_t helper_float_cvts_pu(uint32_t wth0)
{
    uint32_t wt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wt2 = wth0;
    update_fcr31();
    return wt2;
}

uint32_t helper_float_cvtw_s(uint32_t fst0)
{
    uint32_t wt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint32_t helper_float_cvtw_d(uint64_t fdt0)
{
    uint32_t wt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint64_t helper_float_roundl_d(uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint64_t helper_float_roundl_s(uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint32_t helper_float_roundw_d(uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint32_t helper_float_roundw_s(uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint64_t helper_float_truncl_d(uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64_round_to_zero(fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint64_t helper_float_truncl_s(uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64_round_to_zero(fst0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint32_t helper_float_truncw_d(uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32_round_to_zero(fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint32_t helper_float_truncw_s(uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32_round_to_zero(fst0, &env->active_fpu.fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint64_t helper_float_ceill_d(uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint64_t helper_float_ceill_s(uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint32_t helper_float_ceilw_d(uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint32_t helper_float_ceilw_s(uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint64_t helper_float_floorl_d(uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint64_t helper_float_floorl_s(uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        dt2 = FLOAT_SNAN64;
    return dt2;
}

uint32_t helper_float_floorw_d(uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

uint32_t helper_float_floorw_s(uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & (FP_OVERFLOW | FP_INVALID))
        wt2 = FLOAT_SNAN32;
    return wt2;
}

/* unary operations, not modifying fp status  */
#define FLOAT_UNOP(name)                                       \
uint64_t helper_float_ ## name ## _d(uint64_t fdt0)                \
{                                                              \
    return float64_ ## name(fdt0);                             \
}                                                              \
uint32_t helper_float_ ## name ## _s(uint32_t fst0)                \
{                                                              \
    return float32_ ## name(fst0);                             \
}                                                              \
uint64_t helper_float_ ## name ## _ps(uint64_t fdt0)               \
{                                                              \
    uint32_t wt0;                                              \
    uint32_t wth0;                                             \
                                                               \
    wt0 = float32_ ## name(fdt0 & 0XFFFFFFFF);                 \
    wth0 = float32_ ## name(fdt0 >> 32);                       \
    return ((uint64_t)wth0 << 32) | wt0;                       \
}
FLOAT_UNOP(abs)
FLOAT_UNOP(chs)
#undef FLOAT_UNOP

/* MIPS specific unary operations */
uint64_t helper_float_recip_d(uint64_t fdt0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float64_div(FLOAT_ONE64, fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint32_t helper_float_recip_s(uint32_t fst0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_div(FLOAT_ONE32, fst0, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint64_t helper_float_rsqrt_d(uint64_t fdt0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    fdt2 = float64_div(FLOAT_ONE64, fdt2, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint32_t helper_float_rsqrt_s(uint32_t fst0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    fst2 = float32_div(FLOAT_ONE32, fst2, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint64_t helper_float_recip1_d(uint64_t fdt0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float64_div(FLOAT_ONE64, fdt0, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint32_t helper_float_recip1_s(uint32_t fst0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_div(FLOAT_ONE32, fst0, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint64_t helper_float_recip1_ps(uint64_t fdt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_div(FLOAT_ONE32, fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = float32_div(FLOAT_ONE32, fdt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_rsqrt1_d(uint64_t fdt0)
{
    uint64_t fdt2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    fdt2 = float64_div(FLOAT_ONE64, fdt2, &env->active_fpu.fp_status);
    update_fcr31();
    return fdt2;
}

uint32_t helper_float_rsqrt1_s(uint32_t fst0)
{
    uint32_t fst2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    fst2 = float32_div(FLOAT_ONE32, fst2, &env->active_fpu.fp_status);
    update_fcr31();
    return fst2;
}

uint64_t helper_float_rsqrt1_ps(uint64_t fdt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_sqrt(fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = float32_sqrt(fdt0 >> 32, &env->active_fpu.fp_status);
    fst2 = float32_div(FLOAT_ONE32, fst2, &env->active_fpu.fp_status);
    fsth2 = float32_div(FLOAT_ONE32, fsth2, &env->active_fpu.fp_status);
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

#define FLOAT_OP(name, p) void helper_float_##name##_##p(void)

/* binary operations */
#define FLOAT_BINOP(name)                                          \
uint64_t helper_float_ ## name ## _d(uint64_t fdt0, uint64_t fdt1)     \
{                                                                  \
    uint64_t dt2;                                                  \
                                                                   \
    set_float_exception_flags(0, &env->active_fpu.fp_status);            \
    dt2 = float64_ ## name (fdt0, fdt1, &env->active_fpu.fp_status);     \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INVALID)                \
        dt2 = FLOAT_QNAN64;                                        \
    return dt2;                                                    \
}                                                                  \
                                                                   \
uint32_t helper_float_ ## name ## _s(uint32_t fst0, uint32_t fst1)     \
{                                                                  \
    uint32_t wt2;                                                  \
                                                                   \
    set_float_exception_flags(0, &env->active_fpu.fp_status);            \
    wt2 = float32_ ## name (fst0, fst1, &env->active_fpu.fp_status);     \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INVALID)                \
        wt2 = FLOAT_QNAN32;                                        \
    return wt2;                                                    \
}                                                                  \
                                                                   \
uint64_t helper_float_ ## name ## _ps(uint64_t fdt0, uint64_t fdt1)    \
{                                                                  \
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;                             \
    uint32_t fsth0 = fdt0 >> 32;                                   \
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;                             \
    uint32_t fsth1 = fdt1 >> 32;                                   \
    uint32_t wt2;                                                  \
    uint32_t wth2;                                                 \
                                                                   \
    set_float_exception_flags(0, &env->active_fpu.fp_status);            \
    wt2 = float32_ ## name (fst0, fst1, &env->active_fpu.fp_status);     \
    wth2 = float32_ ## name (fsth0, fsth1, &env->active_fpu.fp_status);  \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INVALID) {              \
        wt2 = FLOAT_QNAN32;                                        \
        wth2 = FLOAT_QNAN32;                                       \
    }                                                              \
    return ((uint64_t)wth2 << 32) | wt2;                           \
}

FLOAT_BINOP(add)
FLOAT_BINOP(sub)
FLOAT_BINOP(mul)
FLOAT_BINOP(div)
#undef FLOAT_BINOP

/* ternary operations */
#define FLOAT_TERNOP(name1, name2)                                        \
uint64_t helper_float_ ## name1 ## name2 ## _d(uint64_t fdt0, uint64_t fdt1,  \
                                           uint64_t fdt2)                 \
{                                                                         \
    fdt0 = float64_ ## name1 (fdt0, fdt1, &env->active_fpu.fp_status);          \
    return float64_ ## name2 (fdt0, fdt2, &env->active_fpu.fp_status);          \
}                                                                         \
                                                                          \
uint32_t helper_float_ ## name1 ## name2 ## _s(uint32_t fst0, uint32_t fst1,  \
                                           uint32_t fst2)                 \
{                                                                         \
    fst0 = float32_ ## name1 (fst0, fst1, &env->active_fpu.fp_status);          \
    return float32_ ## name2 (fst0, fst2, &env->active_fpu.fp_status);          \
}                                                                         \
                                                                          \
uint64_t helper_float_ ## name1 ## name2 ## _ps(uint64_t fdt0, uint64_t fdt1, \
                                            uint64_t fdt2)                \
{                                                                         \
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;                                    \
    uint32_t fsth0 = fdt0 >> 32;                                          \
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;                                    \
    uint32_t fsth1 = fdt1 >> 32;                                          \
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;                                    \
    uint32_t fsth2 = fdt2 >> 32;                                          \
                                                                          \
    fst0 = float32_ ## name1 (fst0, fst1, &env->active_fpu.fp_status);          \
    fsth0 = float32_ ## name1 (fsth0, fsth1, &env->active_fpu.fp_status);       \
    fst2 = float32_ ## name2 (fst0, fst2, &env->active_fpu.fp_status);          \
    fsth2 = float32_ ## name2 (fsth0, fsth2, &env->active_fpu.fp_status);       \
    return ((uint64_t)fsth2 << 32) | fst2;                                \
}

FLOAT_TERNOP(mul, add)
FLOAT_TERNOP(mul, sub)
#undef FLOAT_TERNOP

/* negated ternary operations */
#define FLOAT_NTERNOP(name1, name2)                                       \
uint64_t helper_float_n ## name1 ## name2 ## _d(uint64_t fdt0, uint64_t fdt1, \
                                           uint64_t fdt2)                 \
{                                                                         \
    fdt0 = float64_ ## name1 (fdt0, fdt1, &env->active_fpu.fp_status);          \
    fdt2 = float64_ ## name2 (fdt0, fdt2, &env->active_fpu.fp_status);          \
    return float64_chs(fdt2);                                             \
}                                                                         \
                                                                          \
uint32_t helper_float_n ## name1 ## name2 ## _s(uint32_t fst0, uint32_t fst1, \
                                           uint32_t fst2)                 \
{                                                                         \
    fst0 = float32_ ## name1 (fst0, fst1, &env->active_fpu.fp_status);          \
    fst2 = float32_ ## name2 (fst0, fst2, &env->active_fpu.fp_status);          \
    return float32_chs(fst2);                                             \
}                                                                         \
                                                                          \
uint64_t helper_float_n ## name1 ## name2 ## _ps(uint64_t fdt0, uint64_t fdt1,\
                                           uint64_t fdt2)                 \
{                                                                         \
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;                                    \
    uint32_t fsth0 = fdt0 >> 32;                                          \
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;                                    \
    uint32_t fsth1 = fdt1 >> 32;                                          \
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;                                    \
    uint32_t fsth2 = fdt2 >> 32;                                          \
                                                                          \
    fst0 = float32_ ## name1 (fst0, fst1, &env->active_fpu.fp_status);          \
    fsth0 = float32_ ## name1 (fsth0, fsth1, &env->active_fpu.fp_status);       \
    fst2 = float32_ ## name2 (fst0, fst2, &env->active_fpu.fp_status);          \
    fsth2 = float32_ ## name2 (fsth0, fsth2, &env->active_fpu.fp_status);       \
    fst2 = float32_chs(fst2);                                             \
    fsth2 = float32_chs(fsth2);                                           \
    return ((uint64_t)fsth2 << 32) | fst2;                                \
}

FLOAT_NTERNOP(mul, add)
FLOAT_NTERNOP(mul, sub)
#undef FLOAT_NTERNOP

/* MIPS specific binary operations */
uint64_t helper_float_recip2_d(uint64_t fdt0, uint64_t fdt2)
{
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_sub(fdt2, FLOAT_ONE64, &env->active_fpu.fp_status));
    update_fcr31();
    return fdt2;
}

uint32_t helper_float_recip2_s(uint32_t fst0, uint32_t fst2)
{
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_sub(fst2, FLOAT_ONE32, &env->active_fpu.fp_status));
    update_fcr31();
    return fst2;
}

uint64_t helper_float_recip2_ps(uint64_t fdt0, uint64_t fdt2)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fsth0, fsth2, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_sub(fst2, FLOAT_ONE32, &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_sub(fsth2, FLOAT_ONE32, &env->active_fpu.fp_status));
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_rsqrt2_d(uint64_t fdt0, uint64_t fdt2)
{
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_sub(fdt2, FLOAT_ONE64, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_div(fdt2, FLOAT_TWO64, &env->active_fpu.fp_status));
    update_fcr31();
    return fdt2;
}

uint32_t helper_float_rsqrt2_s(uint32_t fst0, uint32_t fst2)
{
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_sub(fst2, FLOAT_ONE32, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32, &env->active_fpu.fp_status));
    update_fcr31();
    return fst2;
}

uint64_t helper_float_rsqrt2_ps(uint64_t fdt0, uint64_t fdt2)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fsth0, fsth2, &env->active_fpu.fp_status);
    fst2 = float32_sub(fst2, FLOAT_ONE32, &env->active_fpu.fp_status);
    fsth2 = float32_sub(fsth2, FLOAT_ONE32, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32, &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_div(fsth2, FLOAT_TWO32, &env->active_fpu.fp_status));
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_addr_ps(uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fst2;
    uint32_t fsth2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_add (fst0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_add (fst1, fsth1, &env->active_fpu.fp_status);
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_mulr_ps(uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fst2;
    uint32_t fsth2;

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    fst2 = float32_mul (fst0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_mul (fst1, fsth1, &env->active_fpu.fp_status);
    update_fcr31();
    return ((uint64_t)fsth2 << 32) | fst2;
}

/* compare operations */
#define FOP_COND_D(op, cond)                                   \
void helper_cmp_d_ ## op (uint64_t fdt0, uint64_t fdt1, int cc)    \
{                                                              \
    int c = cond;                                              \
    update_fcr31();                                            \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}                                                              \
void helper_cmpabs_d_ ## op (uint64_t fdt0, uint64_t fdt1, int cc) \
{                                                              \
    int c;                                                     \
    fdt0 = float64_abs(fdt0);                                  \
    fdt1 = float64_abs(fdt1);                                  \
    c = cond;                                                  \
    update_fcr31();                                            \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}

static int float64_is_unordered(int sig, float64 a, float64 b STATUS_PARAM)
{
    if (float64_is_signaling_nan(a) ||
        float64_is_signaling_nan(b) ||
        (sig && (float64_is_nan(a) || float64_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float64_is_nan(a) || float64_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(f,   (float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status), 0))
FOP_COND_D(un,  float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status))
FOP_COND_D(eq,  !float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status) && float64_eq(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ueq, float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status)  || float64_eq(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(olt, !float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status) && float64_lt(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ult, float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status)  || float64_lt(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ole, !float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status) && float64_le(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ule, float64_is_unordered(0, fdt1, fdt0, &env->active_fpu.fp_status)  || float64_le(fdt0, fdt1, &env->active_fpu.fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(sf,  (float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status), 0))
FOP_COND_D(ngle,float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status))
FOP_COND_D(seq, !float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status) && float64_eq(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ngl, float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status)  || float64_eq(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(lt,  !float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status) && float64_lt(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(nge, float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status)  || float64_lt(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(le,  !float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status) && float64_le(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ngt, float64_is_unordered(1, fdt1, fdt0, &env->active_fpu.fp_status)  || float64_le(fdt0, fdt1, &env->active_fpu.fp_status))

#define FOP_COND_S(op, cond)                                   \
void helper_cmp_s_ ## op (uint32_t fst0, uint32_t fst1, int cc)    \
{                                                              \
    int c = cond;                                              \
    update_fcr31();                                            \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}                                                              \
void helper_cmpabs_s_ ## op (uint32_t fst0, uint32_t fst1, int cc) \
{                                                              \
    int c;                                                     \
    fst0 = float32_abs(fst0);                                  \
    fst1 = float32_abs(fst1);                                  \
    c = cond;                                                  \
    update_fcr31();                                            \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}

static flag float32_is_unordered(int sig, float32 a, float32 b STATUS_PARAM)
{
    if (float32_is_signaling_nan(a) ||
        float32_is_signaling_nan(b) ||
        (sig && (float32_is_nan(a) || float32_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float32_is_nan(a) || float32_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(f,   (float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status), 0))
FOP_COND_S(un,  float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status))
FOP_COND_S(eq,  !float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status) && float32_eq(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ueq, float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)  || float32_eq(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(olt, !float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status) && float32_lt(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ult, float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)  || float32_lt(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ole, !float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status) && float32_le(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ule, float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)  || float32_le(fst0, fst1, &env->active_fpu.fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(sf,  (float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status), 0))
FOP_COND_S(ngle,float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status))
FOP_COND_S(seq, !float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status) && float32_eq(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ngl, float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)  || float32_eq(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(lt,  !float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status) && float32_lt(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(nge, float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)  || float32_lt(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(le,  !float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status) && float32_le(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ngt, float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)  || float32_le(fst0, fst1, &env->active_fpu.fp_status))

#define FOP_COND_PS(op, condl, condh)                           \
void helper_cmp_ps_ ## op (uint64_t fdt0, uint64_t fdt1, int cc)    \
{                                                               \
    uint32_t fst0 = float32_abs(fdt0 & 0XFFFFFFFF);             \
    uint32_t fsth0 = float32_abs(fdt0 >> 32);                   \
    uint32_t fst1 = float32_abs(fdt1 & 0XFFFFFFFF);             \
    uint32_t fsth1 = float32_abs(fdt1 >> 32);                   \
    int cl = condl;                                             \
    int ch = condh;                                             \
                                                                \
    update_fcr31();                                             \
    if (cl)                                                     \
        SET_FP_COND(cc, env->active_fpu);                       \
    else                                                        \
        CLEAR_FP_COND(cc, env->active_fpu);                     \
    if (ch)                                                     \
        SET_FP_COND(cc + 1, env->active_fpu);                   \
    else                                                        \
        CLEAR_FP_COND(cc + 1, env->active_fpu);                 \
}                                                               \
void helper_cmpabs_ps_ ## op (uint64_t fdt0, uint64_t fdt1, int cc) \
{                                                               \
    uint32_t fst0 = float32_abs(fdt0 & 0XFFFFFFFF);             \
    uint32_t fsth0 = float32_abs(fdt0 >> 32);                   \
    uint32_t fst1 = float32_abs(fdt1 & 0XFFFFFFFF);             \
    uint32_t fsth1 = float32_abs(fdt1 >> 32);                   \
    int cl = condl;                                             \
    int ch = condh;                                             \
                                                                \
    update_fcr31();                                             \
    if (cl)                                                     \
        SET_FP_COND(cc, env->active_fpu);                       \
    else                                                        \
        CLEAR_FP_COND(cc, env->active_fpu);                     \
    if (ch)                                                     \
        SET_FP_COND(cc + 1, env->active_fpu);                   \
    else                                                        \
        CLEAR_FP_COND(cc + 1, env->active_fpu);                 \
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(f,   (float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status), 0),
                 (float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status), 0))
FOP_COND_PS(un,  float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status),
                 float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status))
FOP_COND_PS(eq,  !float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)   && float32_eq(fst0, fst1, &env->active_fpu.fp_status),
                 !float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status) && float32_eq(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ueq, float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)    || float32_eq(fst0, fst1, &env->active_fpu.fp_status),
                 float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status)  || float32_eq(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(olt, !float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)   && float32_lt(fst0, fst1, &env->active_fpu.fp_status),
                 !float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status) && float32_lt(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ult, float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)    || float32_lt(fst0, fst1, &env->active_fpu.fp_status),
                 float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status)  || float32_lt(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ole, !float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)   && float32_le(fst0, fst1, &env->active_fpu.fp_status),
                 !float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status) && float32_le(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ule, float32_is_unordered(0, fst1, fst0, &env->active_fpu.fp_status)    || float32_le(fst0, fst1, &env->active_fpu.fp_status),
                 float32_is_unordered(0, fsth1, fsth0, &env->active_fpu.fp_status)  || float32_le(fsth0, fsth1, &env->active_fpu.fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(sf,  (float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status), 0),
                 (float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status), 0))
FOP_COND_PS(ngle,float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status),
                 float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status))
FOP_COND_PS(seq, !float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)   && float32_eq(fst0, fst1, &env->active_fpu.fp_status),
                 !float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status) && float32_eq(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ngl, float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)    || float32_eq(fst0, fst1, &env->active_fpu.fp_status),
                 float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status)  || float32_eq(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(lt,  !float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)   && float32_lt(fst0, fst1, &env->active_fpu.fp_status),
                 !float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status) && float32_lt(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(nge, float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)    || float32_lt(fst0, fst1, &env->active_fpu.fp_status),
                 float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status)  || float32_lt(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(le,  !float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)   && float32_le(fst0, fst1, &env->active_fpu.fp_status),
                 !float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status) && float32_le(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ngt, float32_is_unordered(1, fst1, fst0, &env->active_fpu.fp_status)    || float32_le(fst0, fst1, &env->active_fpu.fp_status),
                 float32_is_unordered(1, fsth1, fsth0, &env->active_fpu.fp_status)  || float32_le(fsth0, fsth1, &env->active_fpu.fp_status))
