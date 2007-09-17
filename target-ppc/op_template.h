/*
 *  PowerPC emulation micro-operations for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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

/* General purpose registers moves */
void OPPROTO glue(op_load_gpr_T0_gpr, REG) (void)
{
    T0 = env->gpr[REG];
    RETURN();
}

void OPPROTO glue(op_load_gpr_T1_gpr, REG) (void)
{
    T1 = env->gpr[REG];
    RETURN();
}

void OPPROTO glue(op_load_gpr_T2_gpr, REG) (void)
{
    T2 = env->gpr[REG];
    RETURN();
}

void OPPROTO glue(op_store_T0_gpr_gpr, REG) (void)
{
    env->gpr[REG] = T0;
    RETURN();
}

void OPPROTO glue(op_store_T1_gpr_gpr, REG) (void)
{
    env->gpr[REG] = T1;
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_store_T2_gpr_gpr, REG) (void)
{
    env->gpr[REG] = T2;
    RETURN();
}
#endif

#if defined(TARGET_PPCEMB)
void OPPROTO glue(op_load_gpr64_T0_gpr, REG) (void)
{
    T0_64 = env->gpr[REG];
    RETURN();
}

void OPPROTO glue(op_load_gpr64_T1_gpr, REG) (void)
{
    T1_64 = env->gpr[REG];
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_load_gpr64_T2_gpr, REG) (void)
{
    T2_64 = env->gpr[REG];
    RETURN();
}
#endif

void OPPROTO glue(op_store_T0_gpr64_gpr, REG) (void)
{
    env->gpr[REG] = T0_64;
    RETURN();
}

void OPPROTO glue(op_store_T1_gpr64_gpr, REG) (void)
{
    env->gpr[REG] = T1_64;
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_store_T2_gpr64_gpr, REG) (void)
{
    env->gpr[REG] = T2_64;
    RETURN();
}
#endif
#endif /* defined(TARGET_PPCEMB) */

#if REG <= 7
/* Condition register moves */
void OPPROTO glue(op_load_crf_T0_crf, REG) (void)
{
    T0 = env->crf[REG];
    RETURN();
}

void OPPROTO glue(op_load_crf_T1_crf, REG) (void)
{
    T1 = env->crf[REG];
    RETURN();
}

void OPPROTO glue(op_store_T0_crf_crf, REG) (void)
{
    env->crf[REG] = T0;
    RETURN();
}

void OPPROTO glue(op_store_T1_crf_crf, REG) (void)
{
    env->crf[REG] = T1;
    RETURN();
}

/* Floating point condition and status register moves */
void OPPROTO glue(op_load_fpscr_T0_fpscr, REG) (void)
{
    T0 = env->fpscr[REG];
    RETURN();
}

#if REG == 0
void OPPROTO glue(op_store_T0_fpscr_fpscr, REG) (void)
{
    env->fpscr[REG] = (env->fpscr[REG] & 0x9) | (T0 & ~0x9);
    RETURN();
}

void OPPROTO glue(op_clear_fpscr_fpscr, REG) (void)
{
    env->fpscr[REG] = (env->fpscr[REG] & 0x9);
    RETURN();
}
#else
void OPPROTO glue(op_store_T0_fpscr_fpscr, REG) (void)
{
    env->fpscr[REG] = T0;
    RETURN();
}

void OPPROTO glue(op_clear_fpscr_fpscr, REG) (void)
{
    env->fpscr[REG] = 0x0;
    RETURN();
}
#endif

#endif /* REG <= 7 */

/* floating point registers moves */
void OPPROTO glue(op_load_fpr_FT0_fpr, REG) (void)
{
    FT0 = env->fpr[REG];
    RETURN();
}

void OPPROTO glue(op_store_FT0_fpr_fpr, REG) (void)
{
    env->fpr[REG] = FT0;
    RETURN();
}

void OPPROTO glue(op_load_fpr_FT1_fpr, REG) (void)
{
    FT1 = env->fpr[REG];
    RETURN();
}

void OPPROTO glue(op_store_FT1_fpr_fpr, REG) (void)
{
    env->fpr[REG] = FT1;
    RETURN();
}

void OPPROTO glue(op_load_fpr_FT2_fpr, REG) (void)
{
    FT2 = env->fpr[REG];
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_store_FT2_fpr_fpr, REG) (void)
{
    env->fpr[REG] = FT2;
    RETURN();
}
#endif

#undef REG
