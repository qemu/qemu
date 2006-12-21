/*
 *  MIPS emulation micro-operations templates for reg load & store for qemu.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if defined(REG)
void glue(op_load_gpr_T0_gpr, REG) (void)
{
    T0 = env->gpr[REG];
    RETURN();
}

void glue(op_store_T0_gpr_gpr, REG) (void)
{
    env->gpr[REG] = T0;
    RETURN();
}

void glue(op_load_gpr_T1_gpr, REG) (void)
{
    T1 = env->gpr[REG];
    RETURN();
}

void glue(op_store_T1_gpr_gpr, REG) (void)
{
    env->gpr[REG] = T1;
    RETURN();
}

void glue(op_load_gpr_T2_gpr, REG) (void)
{
    T2 = env->gpr[REG];
    RETURN();
}
#endif

#if defined (TN)
#define SET_RESET(treg, tregname)        \
    void glue(op_set, tregname)(void)    \
    {                                    \
        treg = PARAM1;                   \
        RETURN();                        \
    }                                    \
    void glue(op_reset, tregname)(void)  \
    {                                    \
        treg = 0;                        \
        RETURN();                        \
    }                                    \

SET_RESET(T0, _T0)
SET_RESET(T1, _T1)
SET_RESET(T2, _T2)

#undef SET_RESET
#endif
