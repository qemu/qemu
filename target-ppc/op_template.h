/*
 *  PPC emulation micro-operations for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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

void OPPROTO glue(op_load_gpr_T0_gpr, REG)(void)
{
    T0 = regs->gpr[REG];
}

void OPPROTO glue(op_load_gpr_T1_gpr, REG)(void)
{
    T1 = regs->gpr[REG];
}

void OPPROTO glue(op_load_gpr_T2_gpr, REG)(void)
{
    T2 = regs->gpr[REG];
}

void OPPROTO glue(op_store_T0_gpr_gpr, REG)(void)
{
    regs->gpr[REG] = T0;
}

void OPPROTO glue(op_store_T1_gpr_gpr, REG)(void)
{
    regs->gpr[REG] = T1;
}

void OPPROTO glue(op_store_T2_gpr_gpr, REG)(void)
{
    regs->gpr[REG] = T2;
}

#if REG <= 7

void OPPROTO glue(op_load_crf_T0_crf, REG)(void)
{
    T0 = regs->crf[REG];
}

void OPPROTO glue(op_load_crf_T1_crf, REG)(void)
{
    T1 = regs->crf[REG];
}

void OPPROTO glue(op_store_T0_crf_crf, REG)(void)
{
    regs->crf[REG] = T0;
}

void OPPROTO glue(op_store_T1_crf_crf, REG)(void)
{
    regs->crf[REG] = T1;
}

#endif /* REG <= 7 */

/* float moves */

void OPPROTO glue(op_load_FT0_fpr, REG)(void)
{
    FT0 = env->fpr[REG];
}

void OPPROTO glue(op_store_FT0_fpr, REG)(void)
{
    env->fpr[REG] = FT0;
}

#undef REG
