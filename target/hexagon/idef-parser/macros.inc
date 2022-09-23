/*
 *  Copyright(c) 2019-2022 rev.ng Labs Srl. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* Copy rules */
#define fLSBOLD(VAL) (fGETBIT(0, VAL))
#define fSATH(VAL) fSATN(16, VAL)
#define fSATUH(VAL) fSATUN(16, VAL)
#define fVSATH(VAL) fVSATN(16, VAL)
#define fVSATUH(VAL) fVSATUN(16, VAL)
#define fSATUB(VAL) fSATUN(8, VAL)
#define fSATB(VAL) fSATN(8, VAL)
#define fVSATUB(VAL) fVSATUN(8, VAL)
#define fVSATB(VAL) fVSATN(8, VAL)
#define fCALL(A) fWRITE_LR(fREAD_NPC()); fWRITE_NPC(A);
#define fCALLR(A) fWRITE_LR(fREAD_NPC()); fWRITE_NPC(A);
#define fCAST2_8s(A) fSXTN(16, 64, A)
#define fCAST2_8u(A) fZXTN(16, 64, A)
#define fVSATW(A) fVSATN(32, fCAST8_8s(A))
#define fSATW(A) fSATN(32, fCAST8_8s(A))
#define fVSAT(A) fVSATN(32, A)
#define fSAT(A) fSATN(32, A)

/* Ease parsing */
#define f8BITSOF(VAL) ((VAL) ? 0xff : 0x00)
#define fREAD_GP() (Constant_extended ? (0) : GP)
#define fCLIP(DST, SRC, U) (DST = fMIN((1 << U) - 1, fMAX(SRC, -(1 << U))))
#define fBIDIR_ASHIFTL(SRC, SHAMT, REGSTYPE)                            \
    ((SHAMT > 0) ?                                                      \
     (fCAST##REGSTYPE##s(SRC) << SHAMT) :                               \
     (fCAST##REGSTYPE##s(SRC) >> -SHAMT))

#define fBIDIR_LSHIFTL(SRC, SHAMT, REGSTYPE)    \
    ((SHAMT > 0) ?                              \
     (fCAST##REGSTYPE##u(SRC) << SHAMT) :       \
     (fCAST##REGSTYPE##u(SRC) >>> -SHAMT))

#define fBIDIR_ASHIFTR(SRC, SHAMT, REGSTYPE)    \
    ((SHAMT > 0) ?                              \
     (fCAST##REGSTYPE##s(SRC) >> SHAMT) :       \
     (fCAST##REGSTYPE##s(SRC) << -SHAMT))

#define fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) < 0) ? ((fCAST##REGSTYPE(SRC) << ((-(SHAMT)) - 1)) << 1)  \
                   : (fCAST##REGSTYPE(SRC) >> (SHAMT)))

#define fBIDIR_LSHIFTR(SRC, SHAMT, REGSTYPE)                            \
    fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE##u)

#define fSATVALN(N, VAL)                                                \
    fSET_OVERFLOW(                                                      \
        ((VAL) < 0) ? (-(1LL << ((N) - 1))) : ((1LL << ((N) - 1)) - 1)  \
    )

#define fSAT_ORIG_SHL(A, ORIG_REG)                                      \
    (((fCAST4s((fSAT(A)) ^ (fCAST4s(ORIG_REG)))) < 0)                   \
        ? fSATVALN(32, (fCAST4s(ORIG_REG)))                             \
        : ((((ORIG_REG) > 0) && ((A) == 0)) ? fSATVALN(32, (ORIG_REG))  \
                                            : fSAT(A)))

#define fBIDIR_ASHIFTR_SAT(SRC, SHAMT, REGSTYPE)                        \
    (((SHAMT) < 0) ? fSAT_ORIG_SHL((fCAST##REGSTYPE##s(SRC)             \
                        << ((-(SHAMT)) - 1)) << 1, (SRC))               \
                   : (fCAST##REGSTYPE##s(SRC) >> (SHAMT)))

#define fBIDIR_ASHIFTL_SAT(SRC, SHAMT, REGSTYPE)                        \
    (((SHAMT) < 0)                                                      \
     ? ((fCAST##REGSTYPE##s(SRC) >> ((-(SHAMT)) - 1)) >> 1)             \
     : fSAT_ORIG_SHL(fCAST##REGSTYPE##s(SRC) << (SHAMT), (SRC)))

#define fEXTRACTU_BIDIR(INREG, WIDTH, OFFSET)                           \
    (fZXTN(WIDTH, 32, fBIDIR_LSHIFTR((INREG), (OFFSET), 4_8)))

/* Least significant bit operations */
#define fLSBNEW0 fLSBNEW(P0N)
#define fLSBNEW1 fLSBNEW(P1N)
#define fLSBOLDNOT(VAL) fGETBIT(0, ~VAL)
#define fLSBNEWNOT(PRED) (fLSBNEW(~PRED))
#define fLSBNEW0NOT fLSBNEW(~P0N)
#define fLSBNEW1NOT fLSBNEW(~P1N)

/* Assignments */
#define fPCALIGN(IMM) (IMM = IMM & ~3)
#define fWRITE_LR(A) (LR = A)
#define fWRITE_FP(A) (FP = A)
#define fWRITE_SP(A) (SP = A)
/*
 * Note: There is a rule in the parser that matches `PC = ...` and emits
 * a call to `gen_write_new_pc`. We need to call `gen_write_new_pc` to
 * get the correct semantics when there are multiple stores in a packet.
 */
#define fBRANCH(LOC, TYPE) (PC = LOC)
#define fJUMPR(REGNO, TARGET, TYPE) (PC = TARGET)
#define fWRITE_LOOP_REGS0(START, COUNT) SA0 = START; (LC0 = COUNT)
#define fWRITE_LOOP_REGS1(START, COUNT) SA1 = START; (LC1 = COUNT)
#define fWRITE_LC0(VAL) (LC0 = VAL)
#define fWRITE_LC1(VAL) (LC1 = VAL)
#define fSET_LPCFG(VAL) (USR.LPCFG = VAL)
#define fWRITE_P0(VAL) P0 = VAL;
#define fWRITE_P1(VAL) P1 = VAL;
#define fWRITE_P3(VAL) P3 = VAL;
#define fEA_RI(REG, IMM) (EA = REG + IMM)
#define fEA_RRs(REG, REG2, SCALE) (EA = REG + (REG2 << SCALE))
#define fEA_IRs(IMM, REG, SCALE) (EA = IMM + (REG << SCALE))
#define fEA_IMM(IMM) (EA = IMM)
#define fEA_REG(REG) (EA = REG)
#define fEA_BREVR(REG) (EA = fbrev(REG))
#define fEA_GPI(IMM) (EA = fREAD_GP() + IMM)
#define fPM_I(REG, IMM) (REG = REG + IMM)
#define fPM_M(REG, MVAL) (REG = REG + MVAL)
#define fWRITE_NPC(VAL) (PC = VAL)

/* Unary operators */
#define fROUND(A) (A + 0x8000)

/* Binary operators */
#define fSCALE(N, A) (A << N)
#define fASHIFTR(SRC, SHAMT, REGSTYPE) (fCAST##REGSTYPE##s(SRC) >> SHAMT)
#define fLSHIFTR(SRC, SHAMT, REGSTYPE) (SRC >>> SHAMT)
#define fROTL(SRC, SHAMT, REGSTYPE) fROTL(SRC, SHAMT)
#define fASHIFTL(SRC, SHAMT, REGSTYPE) (fCAST##REGSTYPE##s(SRC) << SHAMT)

/* Include fHIDE macros which hide type declarations */
#define fHIDE(A) A

/* Purge non-relavant parts */
#define fBRANCH_SPECULATE_STALL(A, B, C, D, E)
