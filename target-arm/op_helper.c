/*
 *  ARM helper routines
 * 
 *  Copyright (c) 2005 CodeSourcery, LLC
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <math.h>
#include <fenv.h>
#include "exec.h"

/* If the host doesn't define C99 math intrinsics then use the normal
   operators.  This may generate excess exceptions, but it's probably
   near enough for most things.  */
#ifndef isless
#define isless(x, y) (x < y)
#endif
#ifndef isgreater
#define isgreater(x, y) (x > y)
#endif
#ifndef isunordered
#define isunordered(x, y) (!((x < y) || (x >= y)))
#endif

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}

/* thread support */

spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

/* VFP support.  */

void do_vfp_abss(void)
{
  FT0s = fabsf(FT0s);
}

void do_vfp_absd(void)
{
  FT0d = fabs(FT0d);
}

void do_vfp_sqrts(void)
{
  FT0s = sqrtf(FT0s);
}

void do_vfp_sqrtd(void)
{
  FT0d = sqrt(FT0d);
}

/* We use an == operator first to generate teh correct floating point
   exception.  Subsequent comparisons use the exception-safe macros.  */
#define DO_VFP_cmp(p)                     \
void do_vfp_cmp##p(void)                  \
{                                         \
    uint32_t flags;                       \
    if (FT0##p == FT1##p)                 \
        flags = 0xc;                      \
    else if (isless (FT0##p, FT1##p))     \
        flags = 0x8;                      \
    else if (isgreater (FT0##p, FT1##p))  \
        flags = 0x2;                      \
    else /* unordered */                  \
        flags = 0x3;                      \
    env->vfp.fpscr = (flags << 28) | (env->vfp.fpscr & 0x0fffffff); \
    FORCE_RET();                          \
}
DO_VFP_cmp(s)
DO_VFP_cmp(d)
#undef DO_VFP_cmp

/* We use a > operator first to get FP exceptions right.  */
#define DO_VFP_cmpe(p)                      \
void do_vfp_cmpe##p(void)                   \
{                                           \
    uint32_t flags;                         \
    if (FT0##p > FT1##p)                    \
        flags = 0x2;                        \
    else if (isless (FT0##p, FT1##p))       \
        flags = 0x8;                        \
    else if (isunordered (FT0##p, FT1##p))  \
        flags = 0x3;                        \
    else /* equal */                        \
        flags = 0xc;                        \
    env->vfp.fpscr = (flags << 28) | (env->vfp.fpscr & 0x0fffffff); \
    FORCE_RET();                            \
}
DO_VFP_cmpe(s)
DO_VFP_cmpe(d)
#undef DO_VFP_cmpe

/* Convert host exception flags to vfp form.  */
int vfp_exceptbits_from_host(int host_bits)
{
    int target_bits = 0;

#ifdef FE_INVALID
    if (host_bits & FE_INVALID)
        target_bits |= 1;
#endif
#ifdef FE_DIVBYZERO
    if (host_bits & FE_DIVBYZERO)
        target_bits |= 2;
#endif
#ifdef FE_OVERFLOW
    if (host_bits & FE_OVERFLOW)
        target_bits |= 4;
#endif
#ifdef FE_UNDERFLOW
    if (host_bits & FE_UNDERFLOW)
        target_bits |= 8;
#endif
#ifdef FE_INEXACT
    if (host_bits & FE_INEXACT)
        target_bits |= 0x10;
#endif
    /* C doesn't define an inexact exception.  */
    return target_bits;
}

/* Convert vfp exception flags to target form.  */
int vfp_host_exceptbits_to_host(int target_bits)
{
    int host_bits = 0;

#ifdef FE_INVALID
    if (target_bits & 1)
        host_bits |= FE_INVALID;
#endif
#ifdef FE_DIVBYZERO
    if (target_bits & 2)
        host_bits |= FE_DIVBYZERO;
#endif
#ifdef FE_OVERFLOW
    if (target_bits & 4)
        host_bits |= FE_OVERFLOW;
#endif
#ifdef FE_UNDERFLOW
    if (target_bits & 8)
        host_bits |= FE_UNDERFLOW;
#endif
#ifdef FE_INEXACT
    if (target_bits & 0x10)
        host_bits |= FE_INEXACT;
#endif
    return host_bits;
}

void do_vfp_set_fpscr(void)
{
    int i;
    uint32_t changed;

    changed = env->vfp.fpscr;
    env->vfp.fpscr = (T0 & 0xffc8ffff);
    env->vfp.vec_len = (T0 >> 16) & 7;
    env->vfp.vec_stride = (T0 >> 20) & 3;

    changed ^= T0;
    if (changed & (3 << 22)) {
        i = (T0 >> 22) & 3;
        switch (i) {
        case 0:
            i = FE_TONEAREST;
            break;
        case 1:
            i = FE_UPWARD;
            break;
        case 2:
            i = FE_DOWNWARD;
            break;
        case 3:
            i = FE_TOWARDZERO;
            break;
        }
        fesetround (i);
    }

    /* Clear host exception flags.  */
    feclearexcept(FE_ALL_EXCEPT);

#ifdef feenableexcept
    if (changed & 0x1f00) {
        i = vfp_exceptbits_to_host((T0 >> 8) & 0x1f);
        feenableexcept (i);
        fedisableexcept (FE_ALL_EXCEPT & ~i);
    }
#endif
    /* XXX: FZ and DN are not implemented.  */
}

void do_vfp_get_fpscr(void)
{
    int i;

    T0 = (env->vfp.fpscr & 0xffc8ffff) | (env->vfp.vec_len << 16)
          | (env->vfp.vec_stride << 20);
    i = fetestexcept(FE_ALL_EXCEPT);
    T0 |= vfp_exceptbits_from_host(i);
}
