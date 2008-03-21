/*
   SPARC micro operations

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "exec.h"
#include "helper.h"

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"

#define MEMSUFFIX _kernel
#include "op_mem.h"

#ifdef TARGET_SPARC64
#define MEMSUFFIX _hypv
#include "op_mem.h"
#endif
#endif

#ifndef TARGET_SPARC64
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void OPPROTO op_save(void)
{
    uint32_t cwp;
    cwp = (env->cwp - 1) & (NWINDOWS - 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_OVF);
    }
    set_cwp(cwp);
    FORCE_RET();
}

void OPPROTO op_restore(void)
{
    uint32_t cwp;
    cwp = (env->cwp + 1) & (NWINDOWS - 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
    FORCE_RET();
}
#else
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void OPPROTO op_save(void)
{
    uint32_t cwp;
    cwp = (env->cwp - 1) & (NWINDOWS - 1);
    if (env->cansave == 0) {
        raise_exception(TT_SPILL | (env->otherwin != 0 ?
                                    (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                    ((env->wstate & 0x7) << 2)));
    } else {
        if (env->cleanwin - env->canrestore == 0) {
            // XXX Clean windows without trap
            raise_exception(TT_CLRWIN);
        } else {
            env->cansave--;
            env->canrestore++;
            set_cwp(cwp);
        }
    }
    FORCE_RET();
}

void OPPROTO op_restore(void)
{
    uint32_t cwp;
    cwp = (env->cwp + 1) & (NWINDOWS - 1);
    if (env->canrestore == 0) {
        raise_exception(TT_FILL | (env->otherwin != 0 ?
                                   (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                   ((env->wstate & 0x7) << 2)));
    } else {
        env->cansave++;
        env->canrestore--;
        set_cwp(cwp);
    }
    FORCE_RET();
}
#endif

void OPPROTO op_jmp_label(void)
{
    GOTO_LABEL_PARAM(1);
}

#ifdef TARGET_SPARC64
void OPPROTO op_flushw(void)
{
    if (env->cansave != NWINDOWS - 2) {
        raise_exception(TT_SPILL | (env->otherwin != 0 ?
                                    (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                    ((env->wstate & 0x7) << 2)));
    }
}

void OPPROTO op_saved(void)
{
    env->cansave++;
    if (env->otherwin == 0)
        env->canrestore--;
    else
        env->otherwin--;
    FORCE_RET();
}

void OPPROTO op_restored(void)
{
    env->canrestore++;
    if (env->cleanwin < NWINDOWS - 1)
        env->cleanwin++;
    if (env->otherwin == 0)
        env->cansave--;
    else
        env->otherwin--;
    FORCE_RET();
}
#endif

#define CHECK_ALIGN_OP(align)                           \
    void OPPROTO op_check_align_T0_ ## align (void)     \
    {                                                   \
        if (T0 & align)                                 \
            raise_exception(TT_UNALIGNED);              \
        FORCE_RET();                                    \
    }

CHECK_ALIGN_OP(1)
CHECK_ALIGN_OP(3)
CHECK_ALIGN_OP(7)
