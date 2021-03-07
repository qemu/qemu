/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_MMVEC_MACROS_H
#define HEXAGON_MMVEC_MACROS_H

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "arch.h"
#include "mmvec/system_ext_mmvec.h"

#ifndef QEMU_GENERATE
#define VdV      (*(MMVector *)(VdV_void))
#define VsV      (*(MMVector *)(VsV_void))
#define VuV      (*(MMVector *)(VuV_void))
#define VvV      (*(MMVector *)(VvV_void))
#define VwV      (*(MMVector *)(VwV_void))
#define VxV      (*(MMVector *)(VxV_void))
#define VyV      (*(MMVector *)(VyV_void))

#define VddV     (*(MMVectorPair *)(VddV_void))
#define VuuV     (*(MMVectorPair *)(VuuV_void))
#define VvvV     (*(MMVectorPair *)(VvvV_void))
#define VxxV     (*(MMVectorPair *)(VxxV_void))

#define QeV      (*(MMQReg *)(QeV_void))
#define QdV      (*(MMQReg *)(QdV_void))
#define QsV      (*(MMQReg *)(QsV_void))
#define QtV      (*(MMQReg *)(QtV_void))
#define QuV      (*(MMQReg *)(QuV_void))
#define QvV      (*(MMQReg *)(QvV_void))
#define QxV      (*(MMQReg *)(QxV_void))
#endif

#define LOG_VTCM_BYTE(VA, MASK, VAL, IDX) \
    do { \
        env->vtcm_log.data.ub[IDX] = (VAL); \
        if (MASK) { \
            set_bit((IDX), env->vtcm_log.mask); \
        } else { \
            clear_bit((IDX), env->vtcm_log.mask); \
        } \
        env->vtcm_log.va[IDX] = (VA); \
    } while (0)

#define fNOTQ(VAL) \
    ({ \
        MMQReg _ret;  \
        int _i_;  \
        for (_i_ = 0; _i_ < fVECSIZE() / 64; _i_++) { \
            _ret.ud[_i_] = ~VAL.ud[_i_]; \
        } \
        _ret;\
     })
#define fGETQBITS(REG, WIDTH, MASK, BITNO) \
    ((MASK) & (REG.w[(BITNO) >> 5] >> ((BITNO) & 0x1f)))
#define fGETQBIT(REG, BITNO) fGETQBITS(REG, 1, 1, BITNO)
#define fGENMASKW(QREG, IDX) \
    (((fGETQBIT(QREG, (IDX * 4 + 0)) ? 0xFF : 0x0) << 0)  | \
     ((fGETQBIT(QREG, (IDX * 4 + 1)) ? 0xFF : 0x0) << 8)  | \
     ((fGETQBIT(QREG, (IDX * 4 + 2)) ? 0xFF : 0x0) << 16) | \
     ((fGETQBIT(QREG, (IDX * 4 + 3)) ? 0xFF : 0x0) << 24))
#define fGETNIBBLE(IDX, SRC) (fSXTN(4, 8, (SRC >> (4 * IDX)) & 0xF))
#define fGETCRUMB(IDX, SRC) (fSXTN(2, 8, (SRC >> (2 * IDX)) & 0x3))
#define fGETCRUMB_SYMMETRIC(IDX, SRC) \
    ((fGETCRUMB(IDX, SRC) >= 0 ? (2 - fGETCRUMB(IDX, SRC)) \
                               : fGETCRUMB(IDX, SRC)))
#define fGENMASKH(QREG, IDX) \
    (((fGETQBIT(QREG, (IDX * 2 + 0)) ? 0xFF : 0x0) << 0) | \
     ((fGETQBIT(QREG, (IDX * 2 + 1)) ? 0xFF : 0x0) << 8))
#define fGETMASKW(VREG, QREG, IDX) (VREG.w[IDX] & fGENMASKW((QREG), IDX))
#define fGETMASKH(VREG, QREG, IDX) (VREG.h[IDX] & fGENMASKH((QREG), IDX))
#define fCONDMASK8(QREG, IDX, YESVAL, NOVAL) \
    (fGETQBIT(QREG, IDX) ? (YESVAL) : (NOVAL))
#define fCONDMASK16(QREG, IDX, YESVAL, NOVAL) \
    ((fGENMASKH(QREG, IDX) & (YESVAL)) | \
     (fGENMASKH(fNOTQ(QREG), IDX) & (NOVAL)))
#define fCONDMASK32(QREG, IDX, YESVAL, NOVAL) \
    ((fGENMASKW(QREG, IDX) & (YESVAL)) | \
     (fGENMASKW(fNOTQ(QREG), IDX) & (NOVAL)))
#define fSETQBITS(REG, WIDTH, MASK, BITNO, VAL) \
    do { \
        uint32_t __TMP = (VAL); \
        REG.w[(BITNO) >> 5] &= ~((MASK) << ((BITNO) & 0x1f)); \
        REG.w[(BITNO) >> 5] |= (((__TMP) & (MASK)) << ((BITNO) & 0x1f)); \
    } while (0)
#define fSETQBIT(REG, BITNO, VAL) fSETQBITS(REG, 1, 1, BITNO, VAL)
#define fVBYTES() (fVECSIZE())
#define fVALIGN(ADDR, LOG2_ALIGNMENT) (ADDR = ADDR & ~(LOG2_ALIGNMENT - 1))
#define fVLASTBYTE(ADDR, LOG2_ALIGNMENT) (ADDR = ADDR | (LOG2_ALIGNMENT - 1))
#define fVELEM(WIDTH) ((fVECSIZE() * 8) / WIDTH)
#define fVECLOGSIZE() (7)
#define fVECSIZE() (1 << fVECLOGSIZE())
#define fSWAPB(A, B) do { uint8_t tmp = A; A = B; B = tmp; } while (0)
#define fV_AL_CHECK(EA, MASK) \
    if ((EA) & (MASK)) { \
        warn("aligning misaligned vector. EA=%08x", (EA)); \
    }
#define fSCATTER_INIT(REGION_START, LENGTH, ELEMENT_SIZE) \
    mem_vector_scatter_init(env)
#define fGATHER_INIT(REGION_START, LENGTH, ELEMENT_SIZE) \
    mem_vector_gather_init(env)
#define fSCATTER_FINISH(OP)
#define fGATHER_FINISH()
#define fLOG_SCATTER_OP(SIZE) \
    do { \
        env->vtcm_log.op = true; \
        env->vtcm_log.op_size = SIZE; \
    } while (0)
#define fVLOG_VTCM_WORD_INCREMENT(EA, OFFSET, INC, IDX, ALIGNMENT, LEN) \
    do { \
        int log_byte = 0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        for (int i0 = 0; i0 < 4; i0++) { \
            log_byte = (va + i0) <= va_high; \
            LOG_VTCM_BYTE(va + i0, log_byte, INC. ub[4 * IDX + i0], \
                          4 * IDX + i0); \
        } \
    } while (0)
#define fVLOG_VTCM_HALFWORD_INCREMENT(EA, OFFSET, INC, IDX, ALIGNMENT, LEN) \
    do { \
        int log_byte = 0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        for (int i0 = 0; i0 < 2; i0++) { \
            log_byte = (va + i0) <= va_high; \
            LOG_VTCM_BYTE(va + i0, log_byte, INC.ub[2 * IDX + i0], \
                          2 * IDX + i0); \
        } \
    } while (0)

#define fVLOG_VTCM_HALFWORD_INCREMENT_DV(EA, OFFSET, INC, IDX, IDX2, IDX_H, \
                                         ALIGNMENT, LEN) \
    do { \
        int log_byte = 0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        for (int i0 = 0; i0 < 2; i0++) { \
            log_byte = (va + i0) <= va_high; \
            LOG_VTCM_BYTE(va + i0, log_byte, INC.ub[2 * IDX + i0], \
                          2 * IDX + i0); \
        } \
    } while (0)

/* NOTE - Will this always be tmp_VRegs[0]; */
#define GATHER_FUNCTION(EA, OFFSET, IDX, LEN, ELEMENT_SIZE, BANK_IDX, QVAL) \
    do { \
        int i0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        uintptr_t ra = GETPC(); \
        int log_bank = 0; \
        int log_byte = 0; \
        for (i0 = 0; i0 < ELEMENT_SIZE; i0++) { \
            log_byte = ((va + i0) <= va_high) && QVAL; \
            log_bank |= (log_byte << i0); \
            uint8_t B; \
            B = cpu_ldub_data_ra(env, EA + i0, ra); \
            env->tmp_VRegs[0].ub[ELEMENT_SIZE * IDX + i0] = B; \
            LOG_VTCM_BYTE(va + i0, log_byte, B, ELEMENT_SIZE * IDX + i0); \
        } \
    } while (0)
#define fVLOG_VTCM_GATHER_WORD(EA, OFFSET, IDX, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, 1); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORD(EA, OFFSET, IDX, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, 1); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORD_DV(EA, OFFSET, IDX, IDX2, IDX_H, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, (2 * IDX2 + IDX_H), 1); \
    } while (0)
#define fVLOG_VTCM_GATHER_WORDQ(EA, OFFSET, IDX, Q, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, \
                        fGETQBIT(QsV, 4 * IDX + i0)); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORDQ(EA, OFFSET, IDX, Q, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, \
                        fGETQBIT(QsV, 2 * IDX + i0)); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORDQ_DV(EA, OFFSET, IDX, IDX2, IDX_H, Q, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, (2 * IDX2 + IDX_H), \
                        fGETQBIT(QsV, 2 * IDX + i0)); \
    } while (0)
#define SCATTER_OP_WRITE_TO_MEM(TYPE) \
    do { \
        uintptr_t ra = GETPC(); \
        for (int i = 0; i < sizeof(MMVector); i += sizeof(TYPE)) { \
            if (test_bit(i, env->vtcm_log.mask)) { \
                TYPE dst = 0; \
                TYPE inc = 0; \
                for (int j = 0; j < sizeof(TYPE); j++) { \
                    uint8_t val; \
                    val = cpu_ldub_data_ra(env, env->vtcm_log.va[i + j], ra); \
                    dst |= val << (8 * j); \
                    inc |= env->vtcm_log.data.ub[j + i] << (8 * j); \
                    clear_bit(j + i, env->vtcm_log.mask); \
                    env->vtcm_log.data.ub[j + i] = 0; \
                } \
                dst += inc; \
                for (int j = 0; j < sizeof(TYPE); j++) { \
                    cpu_stb_data_ra(env, env->vtcm_log.va[i + j], \
                                    (dst >> (8 * j)) & 0xFF, ra); \
                } \
            } \
        } \
    } while (0)
#define SCATTER_OP_PROBE_MEM(TYPE, MMU_IDX, RETADDR) \
    do { \
        for (int i = 0; i < sizeof(MMVector); i += sizeof(TYPE)) { \
            if (test_bit(i, env->vtcm_log.mask)) { \
                for (int j = 0; j < sizeof(TYPE); j++) { \
                    probe_read(env, env->vtcm_log.va[i + j], 1, \
                               MMU_IDX, RETADDR); \
                    probe_write(env, env->vtcm_log.va[i + j], 1, \
                                MMU_IDX, RETADDR); \
                } \
            } \
        } \
    } while (0)
#define SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, ELEM_SIZE, BANK_IDX, QVAL, IN) \
    do { \
        int i0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        int log_bank = 0; \
        int log_byte = 0; \
        for (i0 = 0; i0 < ELEM_SIZE; i0++) { \
            log_byte = ((va + i0) <= va_high) && QVAL; \
            log_bank |= (log_byte << i0); \
            LOG_VTCM_BYTE(va + i0, log_byte, IN.ub[ELEM_SIZE * IDX + i0], \
                          ELEM_SIZE * IDX + i0); \
        } \
    } while (0)
#define fVLOG_VTCM_HALFWORD(EA, OFFSET, IN, IDX, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, 1, IN); \
    } while (0)
#define fVLOG_VTCM_WORD(EA, OFFSET, IN, IDX, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, 1, IN); \
    } while (0)
#define fVLOG_VTCM_HALFWORDQ(EA, OFFSET, IN, IDX, Q, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, \
                         fGETQBIT(QsV, 2 * IDX + i0), IN); \
    } while (0)
#define fVLOG_VTCM_WORDQ(EA, OFFSET, IN, IDX, Q, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, \
                         fGETQBIT(QsV, 4 * IDX + i0), IN); \
    } while (0)
#define fVLOG_VTCM_HALFWORD_DV(EA, OFFSET, IN, IDX, IDX2, IDX_H, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, \
                         (2 * IDX2 + IDX_H), 1, IN); \
    } while (0)
#define fVLOG_VTCM_HALFWORDQ_DV(EA, OFFSET, IN, IDX, Q, IDX2, IDX_H, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, (2 * IDX2 + IDX_H), \
                         fGETQBIT(QsV, 2 * IDX + i0), IN); \
    } while (0)
#define fSTORERELEASE(EA, TYPE) \
    do { \
        fV_AL_CHECK(EA, fVECSIZE() - 1); \
    } while (0)
#ifdef QEMU_GENERATE
#define fLOADMMV(EA, DST) gen_vreg_load(ctx, DST##_off, EA, true)
#endif
#ifdef QEMU_GENERATE
#define fLOADMMVU(EA, DST) gen_vreg_load(ctx, DST##_off, EA, false)
#endif
#ifdef QEMU_GENERATE
#define fSTOREMMV(EA, SRC) \
    gen_vreg_store(ctx, insn, pkt, EA, SRC##_off, insn->slot, true)
#endif
#ifdef QEMU_GENERATE
#define fSTOREMMVQ(EA, SRC, MASK) \
    gen_vreg_masked_store(ctx, EA, SRC##_off, MASK##_off, insn->slot, false)
#endif
#ifdef QEMU_GENERATE
#define fSTOREMMVNQ(EA, SRC, MASK) \
    gen_vreg_masked_store(ctx, EA, SRC##_off, MASK##_off, insn->slot, true)
#endif
#ifdef QEMU_GENERATE
#define fSTOREMMVU(EA, SRC) \
    gen_vreg_store(ctx, insn, pkt, EA, SRC##_off, insn->slot, false)
#endif
#define fVFOREACH(WIDTH, VAR) for (VAR = 0; VAR < fVELEM(WIDTH); VAR++)
#define fVARRAY_ELEMENT_ACCESS(ARRAY, TYPE, INDEX) \
    ARRAY.v[(INDEX) / (fVECSIZE() / (sizeof(ARRAY.TYPE[0])))].TYPE[(INDEX) % \
    (fVECSIZE() / (sizeof(ARRAY.TYPE[0])))]

#define fVSATDW(U, V) fVSATW(((((long long)U) << 32) | fZXTN(32, 64, V)))
#define fVASL_SATHI(U, V) fVSATW(((U) << 1) | ((V) >> 31))
#define fVUADDSAT(WIDTH, U, V) \
    fVSATUN(WIDTH, fZXTN(WIDTH, 2 * WIDTH, U) + fZXTN(WIDTH, 2 * WIDTH, V))
#define fVSADDSAT(WIDTH, U, V) \
    fVSATN(WIDTH, fSXTN(WIDTH, 2 * WIDTH, U) + fSXTN(WIDTH, 2 * WIDTH, V))
#define fVUSUBSAT(WIDTH, U, V) \
    fVSATUN(WIDTH, fZXTN(WIDTH, 2 * WIDTH, U) - fZXTN(WIDTH, 2 * WIDTH, V))
#define fVSSUBSAT(WIDTH, U, V) \
    fVSATN(WIDTH, fSXTN(WIDTH, 2 * WIDTH, U) - fSXTN(WIDTH, 2 * WIDTH, V))
#define fVAVGU(WIDTH, U, V) \
    ((fZXTN(WIDTH, 2 * WIDTH, U) + fZXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVAVGURND(WIDTH, U, V) \
    ((fZXTN(WIDTH, 2 * WIDTH, U) + fZXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1)
#define fVNAVGU(WIDTH, U, V) \
    ((fZXTN(WIDTH, 2 * WIDTH, U) - fZXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVNAVGURNDSAT(WIDTH, U, V) \
    fVSATUN(WIDTH, ((fZXTN(WIDTH, 2 * WIDTH, U) - \
                     fZXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1))
#define fVAVGS(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) + fSXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVAVGSRND(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) + fSXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1)
#define fVNAVGS(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) - fSXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVNAVGSRND(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) - fSXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1)
#define fVNAVGSRNDSAT(WIDTH, U, V) \
    fVSATN(WIDTH, ((fSXTN(WIDTH, 2 * WIDTH, U) - \
                    fSXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1))
#define fVNOROUND(VAL, SHAMT) VAL
#define fVNOSAT(VAL) VAL
#define fVROUND(VAL, SHAMT) \
    ((VAL) + (((SHAMT) > 0) ? (1LL << ((SHAMT) - 1)) : 0))
#define fCARRY_FROM_ADD32(A, B, C) \
    (((fZXTN(32, 64, A) + fZXTN(32, 64, B) + C) >> 32) & 1)
#define fUARCH_NOTE_PUMP_4X()
#define fUARCH_NOTE_PUMP_2X()

#define IV1DEAD()
#endif
