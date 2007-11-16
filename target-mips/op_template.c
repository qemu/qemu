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
    T0 = env->gpr[REG][env->current_tc];
    FORCE_RET();
}

void glue(op_store_T0_gpr_gpr, REG) (void)
{
    env->gpr[REG][env->current_tc] = T0;
    FORCE_RET();
}

void glue(op_load_gpr_T1_gpr, REG) (void)
{
    T1 = env->gpr[REG][env->current_tc];
    FORCE_RET();
}

void glue(op_store_T1_gpr_gpr, REG) (void)
{
    env->gpr[REG][env->current_tc] = T1;
    FORCE_RET();
}

void glue(op_load_gpr_T2_gpr, REG) (void)
{
    T2 = env->gpr[REG][env->current_tc];
    FORCE_RET();
}


void glue(op_load_srsgpr_T0_gpr, REG) (void)
{
    T0 = env->gpr[REG][(env->CP0_SRSCtl >> CP0SRSCtl_PSS) & 0xf];
    FORCE_RET();
}

void glue(op_store_T0_srsgpr_gpr, REG) (void)
{
    env->gpr[REG][(env->CP0_SRSCtl >> CP0SRSCtl_PSS) & 0xf] = T0;
    FORCE_RET();
}
#endif

#if defined (TN)
#define SET_RESET(treg, tregname)        \
    void glue(op_set, tregname)(void)    \
    {                                    \
        treg = (int32_t)PARAM1;          \
        FORCE_RET();                     \
    }                                    \
    void glue(op_reset, tregname)(void)  \
    {                                    \
        treg = 0;                        \
        FORCE_RET();                     \
    }                                    \

SET_RESET(T0, _T0)
SET_RESET(T1, _T1)
SET_RESET(T2, _T2)

#undef SET_RESET

#if defined(TARGET_MIPS64)
#define SET64(treg, tregname)                               \
    void glue(op_set64, tregname)(void)                     \
    {                                                       \
        treg = ((uint64_t)PARAM1 << 32) | (uint32_t)PARAM2; \
        FORCE_RET();                                        \
    }

SET64(T0, _T0)
SET64(T1, _T1)
SET64(T2, _T2)

#undef SET64

#endif
#endif
