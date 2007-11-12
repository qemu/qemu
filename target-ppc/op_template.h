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

/* General purpose registers containing vector operands moves */
#if !defined(TARGET_PPC64)
void OPPROTO glue(op_load_gpr64_T0_gpr, REG) (void)
{
    T0_64 = (uint64_t)env->gpr[REG] | ((uint64_t)env->gprh[REG] << 32);
    RETURN();
}

void OPPROTO glue(op_load_gpr64_T1_gpr, REG) (void)
{
    T1_64 = (uint64_t)env->gpr[REG] | ((uint64_t)env->gprh[REG] << 32);
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_load_gpr64_T2_gpr, REG) (void)
{
    T2_64 = (uint64_t)env->gpr[REG] | ((uint64_t)env->gprh[REG] << 32);
    RETURN();
}
#endif

void OPPROTO glue(op_store_T0_gpr64_gpr, REG) (void)
{
    env->gpr[REG] = T0_64;
    env->gprh[REG] = T0_64 >> 32;
    RETURN();
}

void OPPROTO glue(op_store_T1_gpr64_gpr, REG) (void)
{
    env->gpr[REG] = T1_64;
    env->gprh[REG] = T1_64 >> 32;
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_store_T2_gpr64_gpr, REG) (void)
{
    env->gpr[REG] = T2_64;
    env->gprh[REG] = T2_64 >> 32;
    RETURN();
}
#endif
#endif /* !defined(TARGET_PPC64) */

/* Altivec registers moves */
void OPPROTO glue(op_load_avr_A0_avr, REG) (void)
{
    AVR0 = env->avr[REG];
    RETURN();
}

void OPPROTO glue(op_load_avr_A1_avr, REG) (void)
{
    AVR1 = env->avr[REG];
    RETURN();
}

void OPPROTO glue(op_load_avr_A2_avr, REG) (void)
{
    AVR2 = env->avr[REG];
    RETURN();
}

void OPPROTO glue(op_store_A0_avr_avr, REG) (void)
{
    env->avr[REG] = AVR0;
    RETURN();
}

void OPPROTO glue(op_store_A1_avr_avr, REG) (void)
{
    env->avr[REG] = AVR1;
    RETURN();
}

#if 0 // unused
void OPPROTO glue(op_store_A2_avr_avr, REG) (void)
{
    env->avr[REG] = AVR2;
    RETURN();
}
#endif

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

#if 0 // Unused
void OPPROTO glue(op_store_T1_crf_crf, REG) (void)
{
    env->crf[REG] = T1;
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
