/*
 *  Alpha emulation cpu micro-operations templates for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

/* Optimized constant loads */
#if REG < 3
void OPPROTO glue(op_reset_T, REG) (void)
{
    glue(T, REG) = 0;
    RETURN();
}

#if !defined(HOST_SPARC) && !defined(HOST_SPARC64)
void OPPROTO glue(op_reset_FT, REG) (void)
{
    glue(FT, REG) = 0;
    RETURN();
}
#else
void OPPROTO glue(op_reset_FT, REG) (void)
{
    glue(helper_reset_FT, REG)();
    RETURN();
}
#endif

/* XXX: This can be great on most RISC machines */
#if !defined(__i386__) && !defined(__x86_64__)
void OPPROTO glue(op_set_s16_T, REG) (void)
{
    glue(T, REG) = (int16_t)PARAM(1);
    RETURN();
}

void OPPROTO glue(op_set_u16_T, REG) (void)
{
    glue(T, REG) = (uint16_t)PARAM(1);
    RETURN();
}
#endif

void OPPROTO glue(op_set_s32_T, REG) (void)
{
    glue(T, REG) = (int32_t)PARAM(1);
    RETURN();
}

void OPPROTO glue(op_set_u32_T, REG) (void)
{
    glue(T, REG) = (uint32_t)PARAM(1);
    RETURN();
}

#if 0 // Qemu does not know how to do this...
void OPPROTO glue(op_set_64_T, REG) (void)
{
    glue(T, REG) = (int64_t)PARAM(1);
    RETURN();
}
#else
void OPPROTO glue(op_set_64_T, REG) (void)
{
    glue(T, REG) = ((int64_t)PARAM(1) << 32) | (int64_t)PARAM(2);
    RETURN();
}
#endif

#endif /* REG < 3 */

/* Fixed-point register moves */
#if REG < 31
void OPPROTO glue(op_load_T0_ir, REG) (void)
{
    T0 = env->ir[REG];
    RETURN();
}

void OPPROTO glue(op_load_T1_ir, REG) (void)
{
    T1 = env->ir[REG];
    RETURN();
}

void OPPROTO glue(op_load_T2_ir, REG) (void)
{
    T2 = env->ir[REG];
    RETURN();
}

void OPPROTO glue(op_store_T0_ir, REG) (void)
{
    env->ir[REG] = T0;
    RETURN();
}

void OPPROTO glue(op_store_T1_ir, REG) (void)
{
    env->ir[REG] = T1;
    RETURN();
}

void OPPROTO glue(op_store_T2_ir, REG) (void)
{
    env->ir[REG] = T2;
    RETURN();
}

void OPPROTO glue(op_cmov_ir, REG) (void)
{
    if (T0)
        env->ir[REG] = T1;
    RETURN();
}

/* floating point registers moves */
void OPPROTO glue(op_load_FT0_fir, REG) (void)
{
    FT0 = env->fir[REG];
    RETURN();
}

void OPPROTO glue(op_load_FT1_fir, REG) (void)
{
    FT1 = env->fir[REG];
    RETURN();
}

void OPPROTO glue(op_load_FT2_fir, REG) (void)
{
    FT2 = env->fir[REG];
    RETURN();
}

void OPPROTO glue(op_store_FT0_fir, REG) (void)
{
    env->fir[REG] = FT0;
    RETURN();
}

void OPPROTO glue(op_store_FT1_fir, REG) (void)
{
    env->fir[REG] = FT1;
    RETURN();
}

void OPPROTO glue(op_store_FT2_fir, REG) (void)
{
    env->fir[REG] = FT2;
    RETURN();
}

void OPPROTO glue(op_cmov_fir, REG) (void)
{
    helper_cmov_fir(REG);
    RETURN();
}
#endif /* REG < 31 */

#undef REG
