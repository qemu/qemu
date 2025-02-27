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

#ifndef HEXAGON_MMVEC_MACROS_AUTO_H
#define HEXAGON_MMVEC_MACROS_AUTO_H


#include "mmvec/macros.h"

#include "q6v_defines.h"
#pragma GCC diagnostic ignored "-Wtype-limits"
#define fDUMPQ(STR,REG) do { printf(STR ":" #REG ": 0x%016llx\n",REG.ud[0]); } while (0)
#define fRT8NOTE()
#define fEXPERIMENTAL()
#define fBFLOAT()
#define fCVI_VX_NO_TMP_LD()
#define fNOTQ(VAL) ({mmqreg_t _ret ={0}; int _i_; for (_i_ = 0; _i_ < fVECSIZE()/64; _i_++) _ret.ud[_i_] = ~VAL.ud[_i_]; _ret;})
#define fGETQBITS(REG,WIDTH,MASK,BITNO) ((MASK) & (REG.w[(BITNO)>>5] >> ((BITNO) & 0x1f)))
#define fGETQBIT(REG,BITNO) fGETQBITS(REG,1,1,BITNO)
#define fGENMASKW(QREG,IDX) (((fGETQBIT(QREG,(IDX*4+0)) ? 0xFF : 0x0) << 0) |((fGETQBIT(QREG,(IDX*4+1)) ? 0xFF : 0x0) << 8) |((fGETQBIT(QREG,(IDX*4+2)) ? 0xFF : 0x0) << 16) |((fGETQBIT(QREG,(IDX*4+3)) ? 0xFF : 0x0) << 24))
#define fGET10BIT(COE,VAL,POS) { COE = (((((fGETUBYTE(3,VAL) >> (2 * POS)) & 3) << 8) | fGETUBYTE(POS,VAL)) << 6); COE >>= 6; }
#define fVMAX(X,Y) (X>Y) ? X : Y
#define fREAD_VEC(DST,IDX) (DST = READ_VREG(fMODCIRCU((IDX),5)))
#define fREAD_ZVEC(DST,IDX) (DST = READ_ZREG(fMODCIRCU((IDX),5)))
#define fREAD_ZVEC_WORD(DST,IDX) { mmvector_t ZReg = READ_ZREG(0); DST = ZReg.uw[IDX]; }
#define fREAD_ZVEC_ALL(DST,N,NZ) { int __idx = 0; for (__idx = 0; __idx < NZ/N; __idx++) { memcpy(&DST[N*__idx], &THREAD2STRUCT->ZRegs[__idx], N); } }
#define fZREGB(Z,IDX) ((size1s_t)Z[IDX])
#define fZREGUB(Z,IDX) ((size1u_t)Z[IDX])
#define fZREGH(Z,IDX) ((size2s_t)Z[IDX])
#define fZREGUB(Z,IDX) ((size1u_t)Z[IDX])
#define fGETNIBBLE(IDX,SRC) ( fSXTN(4,8,(SRC >> (4*IDX)) & 0xF) )
#define fGETCRUMB(IDX,SRC) ( fSXTN(2,8,(SRC >> (2*IDX)) & 0x3) )
#define fGETCRUMB_SYMMETRIC(IDX,SRC) ( (fGETCRUMB(IDX,SRC)>=0 ? (2-fGETCRUMB(IDX,SRC)) : fGETCRUMB(IDX,SRC) ) )
#define fWRITE_VEC(IDX,VAR) (WRITE_VREG(fMODCIRCU((IDX),5),VAR))
#define fGENMASKH(QREG,IDX) (((fGETQBIT(QREG,(IDX*2+0)) ? 0xFF : 0x0) << 0) |((fGETQBIT(QREG,(IDX*2+1)) ? 0xFF : 0x0) << 8))
#define fGETMASKW(VREG,QREG,IDX) (VREG.w[IDX] & fGENMASKW((QREG),IDX))
#define fGETMASKH(VREG,QREG,IDX) (VREG.h[IDX] & fGENMASKH((QREG),IDX))
#define fCONDMASK8(QREG,IDX,YESVAL,NOVAL) (fGETQBIT(QREG,IDX) ? (YESVAL) : (NOVAL))
#define fCONDMASK16(QREG,IDX,YESVAL,NOVAL) ((fGENMASKH(QREG,IDX) & (YESVAL)) | (fGENMASKH(fNOTQ(QREG),IDX) & (NOVAL)))
#define fCONDMASK32(QREG,IDX,YESVAL,NOVAL) ((fGENMASKW(QREG,IDX) & (YESVAL)) | (fGENMASKW(fNOTQ(QREG),IDX) & (NOVAL)))
#define fSETQBITS(REG,WIDTH,MASK,BITNO,VAL) do { size4u_t __TMP = (VAL); REG.w[(BITNO)>>5] &= ~((MASK) << ((BITNO) & 0x1f)); REG.w[(BITNO)>>5] |= (((__TMP) & (MASK)) << ((BITNO) & 0x1f)); } while (0)
#define fSETQBIT(REG,BITNO,VAL) fSETQBITS(REG,1,1,BITNO,VAL)
#define fVBYTES() (fVECSIZE())
#define fVHALVES() (fVECSIZE()/2)
#define fVWORDS() (fVECSIZE()/4)
#define fVDWORDS() (fVECSIZE()/8)
#define fVALIGN(ADDR, LOG2_ALIGNMENT) ( ADDR = ADDR & ~(LOG2_ALIGNMENT-1))
#define fVLASTBYTE(ADDR, LOG2_ALIGNMENT) ( ADDR = ADDR | (LOG2_ALIGNMENT-1))
#define fVELEM(WIDTH) ((fVECSIZE()*8)/WIDTH)
#define fVECLOGSIZE() (MAX_VEC_SIZE_LOGBYTES)
#define fVBUF_IDX(EA) (((EA) >> fVECLOGSIZE()) & 0xFF)
#define fREAD_VBUF(IDX,WIDX) READ_VBUF(IDX,WIDX)
#define fLOG_VBUF(IDX,VAL,WIDX) LOG_VBUF(IDX,VAL,WIDX)
#define fVECSIZE() (1<<fVECLOGSIZE())
#define fSWAPB(A, B) { size1u_t tmp = A; A = B; B = tmp; }
#define fVZERO() mmvec_zero_vector()
#define fNEWVREG(VNUM) ((THREAD2STRUCT->VRegs_updated & (((VRegMask)1)<<VNUM)) ? THREAD2STRUCT->future_VRegs[VNUM] : mmvec_zero_vector())
#define fV_AL_CHECK(EA,MASK) if ((EA) & (MASK)) { warn("aligning misaligned vector. PC=%08x EA=%08x",thread->Regs[REG_PC],(EA)); }
#define fSCATTER_INIT( REGION_START, LENGTH, ELEMENT_SIZE) { mem_vector_scatter_init(thread, insn, REGION_START, LENGTH, ELEMENT_SIZE); if (EXCEPTION_DETECTED) return; }
#define fGATHER_INIT( REGION_START, LENGTH, ELEMENT_SIZE) { mem_vector_gather_init(thread, insn, REGION_START, LENGTH, ELEMENT_SIZE); if (EXCEPTION_DETECTED) return; }
#ifdef CONFIG_USER_ONLY
#define fSCATTER_FINISH(OP)
#define fGATHER_FINISH()
#else
#define fSCATTER_FINISH(OP) { if (EXCEPTION_DETECTED) return; mem_vector_scatter_finish(thread, insn, OP); }
#define fGATHER_FINISH() { if (EXCEPTION_DETECTED) return; mem_vector_gather_finish(thread, insn); }
#endif
#define CHECK_VTCM_PAGE(FLAG, BASE, LENGTH, OFFSET, ALIGNMENT) { int slot = insn->slot; paddr_t pa = thread->mem_access[slot].paddr+OFFSET; pa = pa & ~(ALIGNMENT-1); FLAG = (pa < (thread->mem_access[slot].paddr+LENGTH)); }
#define COUNT_OUT_OF_BOUNDS(FLAG, SIZE) { if (!FLAG) { THREAD2STRUCT->vtcm_log.oob_access += SIZE; warn("Scatter/Gather out of bounds of region"); } }
#define fLOG_SCATTER_OP(SIZE) { thread->vtcm_log.op = 1; thread->vtcm_log.op_size = SIZE; }
#define fVLOG_VTCM_GATHER_WORD(EA,OFFSET,IDX, LEN) { GATHER_FUNCTION(EA,OFFSET,IDX, LEN, 4, IDX, 1); }
#define fVLOG_VTCM_GATHER_HALFWORD(EA,OFFSET,IDX, LEN) { GATHER_FUNCTION(EA,OFFSET,IDX, LEN, 2, IDX, 1); }
#define fVLOG_VTCM_GATHER_HALFWORD_DV(EA,OFFSET,IDX,IDX2,IDX_H, LEN) { GATHER_FUNCTION(EA,OFFSET,IDX, LEN, 2, (2*IDX2+IDX_H), 1); }
#define fVLOG_VTCM_GATHER_WORDQ(EA,OFFSET,IDX, Q, LEN) { GATHER_FUNCTION(EA,OFFSET,IDX, LEN, 4, IDX, fGETQBIT(QsV,4*IDX+i0)); }
#define fVLOG_VTCM_GATHER_HALFWORDQ(EA,OFFSET,IDX, Q, LEN) { GATHER_FUNCTION(EA,OFFSET,IDX, LEN, 2, IDX, fGETQBIT(QsV,2*IDX+i0)); }
#define fVLOG_VTCM_GATHER_HALFWORDQ_DV(EA,OFFSET,IDX,IDX2,IDX_H, Q, LEN) { GATHER_FUNCTION(EA,OFFSET,IDX, LEN, 2, (2*IDX2+IDX_H), fGETQBIT(QsV,2*IDX+i0)); }
#define DEBUG_LOG_ADDR(OFFSET) { if (thread->processor_ptr->arch_proc_options->mmvec_network_addr_log2) { int slot = insn->slot; paddr_t pa = thread->mem_access[slot].paddr+OFFSET; } }
//#define SCATTER_OP_WRITE_TO_MEM(TYPE) { for (int i = 0; i < mmvecx->vtcm_log.size; i+=sizeof(TYPE)) { if ( mmvecx->vtcm_log.mask.ub[i] != 0) { TYPE dst = 0; TYPE inc = 0; for(int j = 0; j < sizeof(TYPE); j++) { dst |= (sim_mem_read1(thread->system_ptr, thread->threadId, mmvecx->vtcm_log.pa[i+j]) << (8*j)); inc |= mmvecx->vtcm_log.data.ub[j+i] << (8*j); mmvecx->vtcm_log.mask.ub[j+i] = 0; mmvecx->vtcm_log.data.ub[j+i] = 0; mmvecx->vtcm_log.offsets.ub[j+i] = 0; } dst += inc; for(int j = 0; j < sizeof(TYPE); j++) { sim_mem_write1(thread->system_ptr,thread->threadId, mmvecx->vtcm_log.pa[i+j], (dst >> (8*j))& 0xFF ); } } } }
#define fVLOG_VTCM_HALFWORD(EA,OFFSET,IN,IDX, LEN) { SCATTER_FUNCTION (EA,OFFSET,IDX, LEN, 2, IDX, 1, IN); }
#define fVLOG_VTCM_WORD(EA,OFFSET,IN,IDX,LEN) { SCATTER_FUNCTION (EA,OFFSET,IDX, LEN, 4, IDX, 1, IN); }
#define fVLOG_VTCM_HALFWORDQ(EA,OFFSET,IN,IDX,Q,LEN) { SCATTER_FUNCTION (EA,OFFSET,IDX, LEN, 2, IDX, fGETQBIT(QsV,2*IDX+i0), IN); }
#define fVLOG_VTCM_WORDQ(EA,OFFSET,IN,IDX,Q,LEN) { SCATTER_FUNCTION (EA,OFFSET,IDX, LEN, 4, IDX, fGETQBIT(QsV,4*IDX+i0), IN); }
#define fVLOG_VTCM_HALFWORD_DV(EA,OFFSET,IN,IDX,IDX2,IDX_H, LEN) { SCATTER_FUNCTION (EA,OFFSET,IDX, LEN, 2, (2*IDX2+IDX_H), 1, IN); }
#define fVLOG_VTCM_HALFWORDQ_DV(EA,OFFSET,IN,IDX,Q,IDX2,IDX_H, LEN) { SCATTER_FUNCTION (EA,OFFSET,IDX, LEN, 2, (2*IDX2+IDX_H), fGETQBIT(QsV,2*IDX+i0), IN); }
#define fSTORERELEASE(EA,TYPE) { fV_AL_CHECK(EA,fVECSIZE()-1); mem_store_release(thread, insn, fVECSIZE(), EA&~(fVECSIZE()-1), EA, TYPE, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#define fVFETCH_AL(EA) { fV_AL_CHECK(EA,fVECSIZE()-1); mem_fetch_vector(thread, insn, EA&~(fVECSIZE()-1), slot, fVECSIZE()); }
#define fLOADMMV_AL(EA, ALIGNMENT, LEN, DST) { fV_AL_CHECK(EA,ALIGNMENT-1); /*thread->last_pkt->double_access_vec = 0;*/ mem_load_vector_oddva(thread, 0, EA&~(ALIGNMENT-1), EA, slot, LEN, &DST.ub[0], LEN, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#ifdef QEMU_GENERATE
#define fLOADMMV(EA, DST) gen_vreg_load(ctx, DST##_off, EA, true)
#else
#define fLOADMMV(EA, DST) fLOADMMV_AL(EA,fVECSIZE(),fVECSIZE(),DST)
#endif
#define fLOADMMZ(EA,DST) { mmvector_t load_vec; fV_AL_CHECK(EA,fVECSIZE()-1); mem_load_vector_oddva(thread, 0, EA&~(fVECSIZE()-1), EA, slot, fVECSIZE(), &load_vec.ub[0], fVECSIZE(), fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); int idx = (EA & 0x80)>0; DST.v[idx] = load_vec; }
#define fLOADZ_LOAD(EA,EAU,WIDTH,DST) {/* thread->last_pkt->ext_slot_cancelled = 0; thread->last_pkt->double_access_vec = 0;*/ int etm_size = ((EA % width) ==0) ? fVECSIZE() : 0; if (thread->processor_ptr->options->testgen_mode) etm_size = ((EA % width) ==0) ? WIDTH : 0; mem_load_vector_oddva(thread, 0, EA, EAU, slot, WIDTH, &DST.ub[0], etm_size, fUSE_LOOKUP_ADDRESS()); }
#define fELSE_CANCELZ() else { /*if (thread->last_pkt) { thread->mem_access[slot].dropped_z = 1; thread->last_pkt->ext_slot_cancelled |= (1<<slot); } */ }
#define fPOST_INC4(R) R+=4;
#define fPOST_INC8(R) R+=8;
#define fPOST_INC16(R) R+=16;
#define fEXTRACTZ(DST,IDX) (DST = READ_ZREG(fMODCIRCU((IDX),5)))
#define fLOADZ_UPDATE(EA,WIDTH,ZN,N,SRC) { mmvector_t Z[2]; Z[0] = READ_ZREG(0); Z[1] = READ_ZREG(1); for(int k = 0; k < WIDTH; k++) { int element_idx = (EA+k)%N; int z_idx = ((EA+k)%ZN)/N; Z[z_idx].ub[element_idx] = SRC.ub[k]; } WRITE_EXT_ZREG(0,Z[0],0); WRITE_EXT_ZREG(1,Z[1],0); }
#define fSTOREZ(EA,WIDTH,ZN,N) { mmvector_t store_vec; mmvector_t maskvec = {0}; mmvector_t Z[2]; Z[0] = READ_ZREG(0); Z[1] = READ_ZREG(1); for(int k = 0; k < WIDTH; k++) { int element_idx = (EA+k)%N; int z_idx = ((EA+k)%ZN)/N; store_vec.ub[k] = Z[z_idx].ub[element_idx]; maskvec.ub[k] = 1; } mem_store_vector_oddva(thread, 0, EA, EA, slot, WIDTH, &store_vec.ub[0], &maskvec.ub[0], 0, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#define fLOADMMVQ(EA,DST,QVAL) do { int __i; fLOADMMV_AL(EA,fVECSIZE(),fVECSIZE(),DST); fVFOREACH(8,__i) if (!fGETQBIT(QVAL,__i)) DST.b[__i] = 0; } while (0)
#define fLOADMMVNQ(EA,DST,QVAL) do { int __i; fLOADMMV_AL(EA,fVECSIZE(),fVECSIZE(),DST); fVFOREACH(8,__i) if (fGETQBIT(QVAL,__i)) DST.b[__i] = 0; } while (0)
#define fLOADMMVU_AL(EA, ALIGNMENT, LEN, DST) { size4u_t size2 = (EA)&(ALIGNMENT-1); size4u_t size1 = LEN-size2; /*thread->last_pkt->double_access_vec = 1;*/ mem_load_vector_oddva(thread, 0, EA+size1, EA+fVECSIZE(), 1, size2, &DST.ub[size1], size2, fUSE_LOOKUP_ADDRESS()); mem_load_vector_oddva(thread, 0, EA, EA, 0, size1, &DST.ub[0], size1, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#ifdef QEMU_GENERATE
#define fLOADMMVU(EA, DST) gen_vreg_load(ctx, DST##_off, EA, false)
#else
#define fLOADMMVU(EA, DST) { /*thread->last_pkt->pkt_has_vtcm_access = 0; thread->last_pkt->pkt_access_count = 0;*/ if ( (EA & (fVECSIZE()-1)) == 0) { /*thread->last_pkt->pkt_has_vmemu_access = 0; thread->last_pkt->double_access = 0;*/ fLOADMMV_AL(EA,fVECSIZE(),fVECSIZE(),DST); } else { /*thread->last_pkt->pkt_has_vmemu_access = 1; thread->last_pkt->double_access = 1;*/ fLOADMMVU_AL(EA,fVECSIZE(),fVECSIZE(),DST); } }
#endif
#define fSTOREMMV_AL(EA, ALIGNMENT, LEN, SRC) { fV_AL_CHECK(EA,ALIGNMENT-1); mem_store_vector_oddva(thread, 0, EA&~(ALIGNMENT-1), EA, slot, LEN, &SRC.ub[0], 0, 0, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#ifdef QEMU_GENERATE
#define fSTOREMMV(EA, SRC) gen_vreg_store(ctx, EA, SRC##_off, insn->slot, true)
#else
#define fSTOREMMV(EA, SRC) fSTOREMMV_AL(EA,fVECSIZE(),fVECSIZE(),SRC)
#endif
#define fSTOREMMVQ_AL(EA, ALIGNMENT, LEN, SRC, MASK) do { mmvector_t maskvec; int i; for (i = 0; i < fVECSIZE(); i++) maskvec.ub[i] = fGETQBIT(MASK,i); mem_store_vector_oddva(thread, 0, EA&~(ALIGNMENT-1), EA, slot, LEN, &SRC.ub[0], &maskvec.ub[0], 0, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); } while (0)
#ifdef QEMU_GENERATE
#define fSTOREMMVQ(EA, SRC, MASK) \
    gen_vreg_masked_store(ctx, EA, SRC##_off, MASK##_off, insn->slot, false)
#else
#define fSTOREMMVQ(EA, SRC, MASK) fSTOREMMVQ_AL(EA,fVECSIZE(),fVECSIZE(),SRC,MASK)
#endif
#define fSTOREMMVNQ_AL(EA, ALIGNMENT, LEN, SRC, MASK) { mmvector_t maskvec; int i; for (i = 0; i < fVECSIZE(); i++) maskvec.ub[i] = fGETQBIT(MASK,i); fV_AL_CHECK(EA,ALIGNMENT-1); mem_store_vector_oddva(thread, 0, EA&~(ALIGNMENT-1), EA, slot, LEN, &SRC.ub[0], &maskvec.ub[0], 1, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#ifdef QEMU_GENERATE
#define fSTOREMMVNQ(EA, SRC, MASK) \
    gen_vreg_masked_store(ctx, EA, SRC##_off, MASK##_off, insn->slot, true)
#else
#define fSTOREMMVNQ(EA, SRC, MASK) fSTOREMMVNQ_AL(EA,fVECSIZE(),fVECSIZE(),SRC,MASK)
#endif
#define fSTOREMMVU_AL(EA, ALIGNMENT, LEN, SRC) { size4u_t size1 = ALIGNMENT-((EA)&(ALIGNMENT-1)); size4u_t size2; if (size1>LEN) size1 = LEN; size2 = LEN-size1; mem_store_vector_oddva(thread, 0, EA+size1, EA+fVECSIZE(), 1, size2, &SRC.ub[size1], 0, 0, fUSE_LOOKUP_ADDRESS()); mem_store_vector_oddva(thread, 0, EA, EA, 0, size1, &SRC.ub[0], 0, 0, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#ifdef QEMU_GENERATE
#define fSTOREMMVU(EA, SRC) \
    gen_vreg_store(ctx, EA, SRC##_off, insn->slot, false)
#else
#define fSTOREMMVU(EA, SRC) { /*thread->last_pkt->pkt_has_vtcm_access = 0; thread->last_pkt->pkt_access_count = 0;*/ if ( (EA & (fVECSIZE()-1)) == 0) { /*thread->last_pkt->double_access = 0;*/ fSTOREMMV_AL(EA,fVECSIZE(),fVECSIZE(),SRC); } else { /*thread->last_pkt->double_access = 1; thread->last_pkt->pkt_has_vmemu_access = 1;*/ fSTOREMMVU_AL(EA,fVECSIZE(),fVECSIZE(),SRC); } }
#endif
#define fSTOREMMVQU_AL(EA, ALIGNMENT, LEN, SRC, MASK) { size4u_t size1 = ALIGNMENT-((EA)&(ALIGNMENT-1)); size4u_t size2; mmvector_t maskvec; int i; for (i = 0; i < fVECSIZE(); i++) maskvec.ub[i] = fGETQBIT(MASK,i); if (size1>LEN) size1 = LEN; size2 = LEN-size1; mem_store_vector_oddva(thread, 0, EA+size1, EA+fVECSIZE(), 1, size2, &SRC.ub[size1], &maskvec.ub[size1], 0, fUSE_LOOKUP_ADDRESS()); mem_store_vector_oddva(thread, 0, EA, 0, size1, &SRC.ub[0], &maskvec.ub[0], 0, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#define fSTOREMMVQU(EA, SRC, MASK) { /*thread->last_pkt->pkt_has_vtcm_access = 0; thread->last_pkt->pkt_access_count = 0;*/ if ( (EA & (fVECSIZE()-1)) == 0) { /*thread->last_pkt->double_access = 0;*/ fSTOREMMVQ_AL(EA,fVECSIZE(),fVECSIZE(),SRC,MASK); } else { /*thread->last_pkt->double_access = 1; thread->last_pkt->pkt_has_vmemu_access = 1;*/ fSTOREMMVQU_AL(EA,fVECSIZE(),fVECSIZE(),SRC,MASK); } }
#define fSTOREMMVNQU_AL(EA, ALIGNMENT, LEN, SRC, MASK) { size4u_t size1 = ALIGNMENT-((EA)&(ALIGNMENT-1)); size4u_t size2; mmvector_t maskvec; int i; for (i = 0; i < fVECSIZE(); i++) maskvec.ub[i] = fGETQBIT(MASK,i); if (size1>LEN) size1 = LEN; size2 = LEN-size1; mem_store_vector_oddva(thread, 0, EA+size1, EA+fVECSIZE(), 1, size2, &SRC.ub[size1], &maskvec.ub[size1], 1, fUSE_LOOKUP_ADDRESS()); mem_store_vector_oddva(thread, 0, EA, EA, 0, size1, &SRC.ub[0], &maskvec.ub[0], 1, fUSE_LOOKUP_ADDRESS_BY_REV(thread->processor_ptr)); }
#define fSTOREMMVNQU(EA, SRC, MASK) { /*thread->last_pkt->pkt_has_vtcm_access = 0; thread->last_pkt->pkt_access_count = 0;*/ if ( (EA & (fVECSIZE()-1)) == 0) { /*thread->last_pkt->double_access = 0;*/ fSTOREMMVNQ_AL(EA,fVECSIZE(),fVECSIZE(),SRC,MASK); } else { /*thread->last_pkt->double_access = 1; thread->last_pkt->pkt_has_vmemu_access = 1;*/ fSTOREMMVNQU_AL(EA,fVECSIZE(),fVECSIZE(),SRC,MASK); } }
#define fVFOREACH(WIDTH, VAR) for (VAR = 0; VAR < fVELEM(WIDTH); VAR++)
#define fVARRAY_ELEMENT_ACCESS(ARRAY, TYPE, INDEX) ARRAY.v[(INDEX) / (fVECSIZE()/(sizeof(ARRAY.TYPE[0])))].TYPE[(INDEX) % (fVECSIZE()/(sizeof(ARRAY.TYPE[0])))]
#define fVNEWCANCEL(REGNUM) do { THREAD2STRUCT->VRegs_select &= ~(1<<(REGNUM)); } while (0)
#define fTMPVDATA() mmvec_vtmp_data(thread)
#define fVSATDW(U,V) fVSATW( ( ( ((long long)U)<<32 ) | fZXTN(32,64,V) ) )
#define fVASL_SATHI(U,V) fVSATW(((U)<<1) | ((V)>>31))
#define fVUADDSAT(WIDTH,U,V) fVSATUN( WIDTH, fZXTN(WIDTH, 2*WIDTH, U) + fZXTN(WIDTH, 2*WIDTH, V))
#define fVSADDSAT(WIDTH,U,V) ({size8s_t tmp5 = fSXTN(WIDTH, 2*WIDTH, U); size8s_t tmp6 = fSXTN(WIDTH, 2*WIDTH, V); size8s_t tmp7 = tmp5 + tmp6; fVSATN( WIDTH, tmp7); })
#define fVUSUBSAT(WIDTH,U,V) fVSATUN( WIDTH, fZXTN(WIDTH, 2*WIDTH, U) - fZXTN(WIDTH, 2*WIDTH, V))
#define fVSSUBSAT(WIDTH,U,V) fVSATN( WIDTH, fSXTN(WIDTH, 2*WIDTH, U) - fSXTN(WIDTH, 2*WIDTH, V))
#define fVAVGU(WIDTH,U,V) ((fZXTN(WIDTH, 2*WIDTH, U) + fZXTN(WIDTH, 2*WIDTH, V))>>1)
#define fVAVGURND(WIDTH,U,V) ((fZXTN(WIDTH, 2*WIDTH, U) + fZXTN(WIDTH, 2*WIDTH, V)+1)>>1)
#define fVNAVGU(WIDTH,U,V) ((fZXTN(WIDTH, 2*WIDTH, U) - fZXTN(WIDTH, 2*WIDTH, V))>>1)
#define fVNAVGURNDSAT(WIDTH,U,V) fVSATUN(WIDTH,((fZXTN(WIDTH, 2*WIDTH, U) - fZXTN(WIDTH, 2*WIDTH, V)+1)>>1))
#define fVAVGS(WIDTH,U,V) ((fSXTN(WIDTH, 2*WIDTH, U) + fSXTN(WIDTH, 2*WIDTH, V))>>1)
#define fVAVGSRND(WIDTH,U,V) ((fSXTN(WIDTH, 2*WIDTH, U) + fSXTN(WIDTH, 2*WIDTH, V)+1)>>1)
#define fVNAVGS(WIDTH,U,V) ((fSXTN(WIDTH, 2*WIDTH, U) - fSXTN(WIDTH, 2*WIDTH, V))>>1)
#define fVNAVGSRND(WIDTH,U,V) ((fSXTN(WIDTH, 2*WIDTH, U) - fSXTN(WIDTH, 2*WIDTH, V)+1)>>1)
#define fVNAVGSRNDSAT(WIDTH,U,V) fVSATN(WIDTH,((fSXTN(WIDTH, 2*WIDTH, U) - fSXTN(WIDTH, 2*WIDTH, V)+1)>>1))
#define fVNOROUND(VAL,SHAMT) VAL
#define fVNOSAT(VAL) VAL
#define fVROUND(VAL,SHAMT) ((VAL) + (((SHAMT)>0)?(1LL<<((SHAMT)-1)):0))
#define fCARRY_FROM_ADD32(A,B,C) (((fZXTN(32,64,A)+fZXTN(32,64,B)+C) >> 32) & 1)
#define fUARCH_NOTE_PUMP_4X()
#define fUARCH_NOTE_PUMP_2X()
#define UNLIKELY(X) __builtin_expect((X), 0)
#define fVDOCHKPAGECROSS(BASE,SUM) if (UNLIKELY(thread->timing_on)) { thread->mem_access[slot].check_page_crosses = 1; thread->mem_access[slot].page_cross_base = BASE; thread->mem_access[slot].page_cross_sum = SUM; }
#define fPARSEQF32(A) parse_qf32(A)
#define fRNDSATQF32(A,B,C) rnd_sat_qf32(A,B,C)
#define fPARSEQF16(A) parse_qf16(A)
#define fRNDSATQF16(A,B,C) rnd_sat_qf16(A,B,C)
#define fPARSESF(A) parse_sf(A)
#define fRNDSATSF(A,B) rnd_sat_sf(A,B)
#define fPARSEHF(A) parse_hf(A)
#define fRNDSATHF(A,B) rnd_sat_hf(A,B)
#define fRNDSATW(A,B) rnd_sat_w(A,B)
#define fRNDSATUW(A,B) rnd_sat_uw(A,B)
#define fRNDSATH(A,B) rnd_sat_h(A,B)
#define fRNDSATUH(A,B) rnd_sat_uh(A,B)
#define fRNDSATB(A,B) rnd_sat_b(A,B)
#define fRNDSATUB(A,B) rnd_sat_ub(A,B)
#define fNEGQF32(A) negate32(A)
#define fNEGQF16(A) negate16(A)
#define fNEGSF(A) negate_sf(A)
#define fNEGHF(A) negate_hf(A)
#define fCMPGT_QF32(A,B) cmpgt_qf32(A,B)
#define fCMPGT_QF16(A,B) cmpgt_qf16(A,B)
#define fCMPGT_SF(A,B) cmpgt_sf(A,B)
#define fCMPGT_HF(A,B) cmpgt_hf(A,B)
#define fCMPGT_BF(A,B) cmpgt_sf(((int)A) << 16,((int)B) << 16)
#define fCMPGT_QF32_SF(A,B) cmpgt_qf32_sf(A,B)
#define fCMPGT_QF16_HF(A,B) cmpgt_qf16_hf(A,B)
#define fMAX_QF32(X,Y) max_qf32(X,Y)
#define fMIN_QF32(X,Y) min_qf32(X,Y)
#define fMAX_QF32_SF(X,Y) max_qf32_sf(X,Y)
#define fMIN_QF32_SF(X,Y) min_qf32_sf(X,Y)
#define fMAX_QF16(X,Y) max_qf16(X,Y)
#define fMIN_QF16(X,Y) min_qf16(X,Y)
#define fMAX_QF16_HF(X,Y) max_qf16_hf(X,Y)
#define fMIN_QF16_HF(X,Y) min_qf16_hf(X,Y)
#define fMAX_SF(X,Y) max_sf(X,Y)
#define fMIN_SF(X,Y) min_sf(X,Y)
#define fMAX_HF(X,Y) max_hf(X,Y)
#define fMIN_HF(X,Y) min_hf(X,Y)

#define fSTOREDOUBLEMMV(EA, SRC) fSTOREMMV_AL(EA,fVECSIZE(),2*fVECSIZE(),SRC)
#endif
