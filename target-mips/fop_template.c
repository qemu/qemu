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

#if defined(FREG)

#define OP_WLOAD_FREG(treg, tregname, FREG)              \
    void glue(glue(op_load_fpr_,tregname), FREG) (void)  \
    {                                                    \
        treg = env->fpu->fpr[FREG].w[FP_ENDIAN_IDX];    \
        FORCE_RET();                                     \
    }

#define OP_WSTORE_FREG(treg, tregname, FREG)             \
    void glue(glue(op_store_fpr_,tregname), FREG) (void) \
    {                                                    \
        env->fpu->fpr[FREG].w[FP_ENDIAN_IDX] = treg;    \
        FORCE_RET();                                     \
    }

/* WT0 = FREG.w: op_load_fpr_WT0_fprFREG */
OP_WLOAD_FREG(WT0, WT0_fpr, FREG)
/* FREG.w = WT0: op_store_fpr_WT0_fprFREG */
OP_WSTORE_FREG(WT0, WT0_fpr, FREG)

OP_WLOAD_FREG(WT1, WT1_fpr, FREG)
OP_WSTORE_FREG(WT1, WT1_fpr, FREG)

OP_WLOAD_FREG(WT2, WT2_fpr, FREG)
OP_WSTORE_FREG(WT2, WT2_fpr, FREG)

#define OP_DLOAD_FREG(treg, tregname, FREG)              \
    void glue(glue(op_load_fpr_,tregname), FREG) (void)  \
    {                                                    \
        if (env->hflags & MIPS_HFLAG_F64)                \
            treg = env->fpu->fpr[FREG].d;                \
        else                                             \
            treg = (uint64_t)(env->fpu->fpr[FREG | 1].w[FP_ENDIAN_IDX]) << 32 | \
                   env->fpu->fpr[FREG & ~1].w[FP_ENDIAN_IDX]; \
        FORCE_RET();                                     \
    }

#define OP_DSTORE_FREG(treg, tregname, FREG)             \
    void glue(glue(op_store_fpr_,tregname), FREG) (void) \
    {                                                    \
        if (env->hflags & MIPS_HFLAG_F64)                \
            env->fpu->fpr[FREG].d = treg;                \
        else {                                           \
            env->fpu->fpr[FREG | 1].w[FP_ENDIAN_IDX] = treg >> 32; \
            env->fpu->fpr[FREG & ~1].w[FP_ENDIAN_IDX] = treg;      \
        }                                                \
        FORCE_RET();                                     \
    }

OP_DLOAD_FREG(DT0, DT0_fpr, FREG)
OP_DSTORE_FREG(DT0, DT0_fpr, FREG)

OP_DLOAD_FREG(DT1, DT1_fpr, FREG)
OP_DSTORE_FREG(DT1, DT1_fpr, FREG)

OP_DLOAD_FREG(DT2, DT2_fpr, FREG)
OP_DSTORE_FREG(DT2, DT2_fpr, FREG)

#define OP_PSLOAD_FREG(treg, tregname, FREG)             \
    void glue(glue(op_load_fpr_,tregname), FREG) (void)  \
    {                                                    \
        treg = env->fpu->fpr[FREG].w[!FP_ENDIAN_IDX];   \
        FORCE_RET();                                     \
    }

#define OP_PSSTORE_FREG(treg, tregname, FREG)            \
    void glue(glue(op_store_fpr_,tregname), FREG) (void) \
    {                                                    \
        env->fpu->fpr[FREG].w[!FP_ENDIAN_IDX] = treg;   \
        FORCE_RET();                                     \
    }

OP_PSLOAD_FREG(WTH0, WTH0_fpr, FREG)
OP_PSSTORE_FREG(WTH0, WTH0_fpr, FREG)

OP_PSLOAD_FREG(WTH1, WTH1_fpr, FREG)
OP_PSSTORE_FREG(WTH1, WTH1_fpr, FREG)

OP_PSLOAD_FREG(WTH2, WTH2_fpr, FREG)
OP_PSSTORE_FREG(WTH2, WTH2_fpr, FREG)

#endif

#if defined (FTN)

#define SET_RESET(treg, tregname)        \
    void glue(op_set, tregname)(void)    \
    {                                    \
        treg = PARAM1;                   \
        FORCE_RET();                     \
    }                                    \
    void glue(op_reset, tregname)(void)  \
    {                                    \
        treg = 0;                        \
        FORCE_RET();                     \
    }

SET_RESET(WT0, _WT0)
SET_RESET(WT1, _WT1)
SET_RESET(WT2, _WT2)
SET_RESET(DT0, _DT0)
SET_RESET(DT1, _DT1)
SET_RESET(DT2, _DT2)
SET_RESET(WTH0, _WTH0)
SET_RESET(WTH1, _WTH1)
SET_RESET(WTH2, _WTH2)

#undef SET_RESET
#endif
