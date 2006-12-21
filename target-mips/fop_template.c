/*
 * MIPS emulation micro-operations templates for floating point reg 
 * load & store for qemu.
 * 
 * Copyright (c) 2006 Marius Groeger
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

#if defined(SFREG)

#define OP_WLOAD_FREG(treg, tregname, SFREG)      \
    void glue(glue(op_load_fpr_,tregname), SFREG) (void) \
    {                                                   \
        treg = FPR_W(env, SFREG);     \
        RETURN();                                       \
    }

#define OP_WSTORE_FREG(treg, tregname, SFREG)            \
    void glue(glue(op_store_fpr_,tregname), SFREG) (void)\
    {                                                   \
        FPR_W(env, SFREG) = treg;     \
        RETURN();                                       \
    }

/* WT0 = SFREG.w: op_load_fpr_WT0_fprSFREG */
OP_WLOAD_FREG(WT0, WT0_fpr, SFREG)
/* SFREG.w = WT0: op_store_fpr_WT0_fprSFREG */
OP_WSTORE_FREG(WT0, WT0_fpr, SFREG)

OP_WLOAD_FREG(WT1, WT1_fpr, SFREG)
OP_WSTORE_FREG(WT1, WT1_fpr, SFREG)

OP_WLOAD_FREG(WT2, WT2_fpr, SFREG)
OP_WSTORE_FREG(WT2, WT2_fpr, SFREG)

#endif

#if defined(DFREG)

#define OP_DLOAD_FREG(treg, tregname, DFREG)      \
    void glue(glue(op_load_fpr_,tregname), DFREG) (void) \
    {                                                   \
        treg = FPR_D(env, DFREG);                    \
        RETURN();                                       \
    }

#define OP_DSTORE_FREG(treg, tregname, DFREG)            \
    void glue(glue(op_store_fpr_,tregname), DFREG) (void)\
    {                                                   \
        FPR_D(env, DFREG) = treg;                    \
        RETURN();                                       \
    }

OP_DLOAD_FREG(DT0, DT0_fpr, DFREG)
OP_DSTORE_FREG(DT0, DT0_fpr, DFREG)

OP_DLOAD_FREG(DT1, DT1_fpr, DFREG)
OP_DSTORE_FREG(DT1, DT1_fpr, DFREG)

OP_DLOAD_FREG(DT2, DT2_fpr, DFREG)
OP_DSTORE_FREG(DT2, DT2_fpr, DFREG)

#endif

#if defined (FTN)

#define SET_RESET(treg, tregname)    \
    void glue(op_set, tregname)(void)    \
    {                                \
        treg = PARAM1;               \
        RETURN();                    \
    }                                \
    void glue(op_reset, tregname)(void)  \
    {                                \
        treg = 0;                    \
        RETURN();                    \
    }                                \

SET_RESET(WT0, _WT0)
SET_RESET(WT1, _WT1)
SET_RESET(WT2, _WT2)
SET_RESET(DT0, _DT0)
SET_RESET(DT1, _DT1)
SET_RESET(DT2, _DT2)

#undef SET_RESET
#endif
