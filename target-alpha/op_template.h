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

#endif /* REG < 3 */

/* Fixed-point register moves */
#if REG < 31
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
