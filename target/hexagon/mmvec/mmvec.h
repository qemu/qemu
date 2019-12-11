/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef MMVEC_H
#define MMVEC_H

#define VECEXT 1

#include <stdint.h>
#include "arch.h"

/* From thread.h */
enum mem_access_types {
    access_type_INVALID = 0,
    access_type_unknown = 1,
    access_type_load = 2,
    access_type_store = 3,
    access_type_fetch = 4,
    access_type_dczeroa = 5,
    access_type_dccleana = 6,
    access_type_dcinva = 7,
    access_type_dccleaninva = 8,
    access_type_icinva = 9,
    access_type_ictagr = 10,
    access_type_ictagw = 11,
    access_type_icdatar = 12,
    access_type_dcfetch = 13,
    access_type_l2fetch = 14,
    access_type_l2cleanidx = 15,
    access_type_l2cleaninvidx = 16,
    access_type_l2tagr = 17,
    access_type_l2tagw = 18,
    access_type_dccleanidx = 19,
    access_type_dcinvidx = 20,
    access_type_dccleaninvidx = 21,
    access_type_dctagr = 22,
    access_type_dctagw = 23,
    access_type_k0unlock = 24,
    access_type_l2locka = 25,
    access_type_l2unlocka = 26,
    access_type_l2kill = 27,
    access_type_l2gclean = 28,
    access_type_l2gcleaninv = 29,
    access_type_l2gunlock = 30,
    access_type_synch = 31,
    access_type_isync = 32,
    access_type_pause = 33,
    access_type_load_phys = 34,
    access_type_load_locked = 35,
    access_type_store_conditional = 36,
    access_type_barrier = 37,
#ifdef CLADE
    access_type_clade = 38,
#endif
    access_type_memcpy_load = 39,
    access_type_memcpy_store = 40,
#ifdef CLADE2
    access_type_clade2 = 41,
#endif
    access_type_hmx_load_act = 42,
    access_type_hmx_load_wei = 43,
    access_type_hmx_load_bias = 44,
    access_type_hmx_store = 45,
    access_type_hmx_store_bias = 46,
    access_type_udma_load = 47,
    access_type_udma_store = 48,

    NUM_CORE_ACCESS_TYPES
};

enum ext_mem_access_types {
    access_type_vload = NUM_CORE_ACCESS_TYPES,
    access_type_vstore,
    access_type_vload_nt,
    access_type_vstore_nt,
    access_type_vgather_load,
    access_type_vscatter_store,
    access_type_vscatter_release,
    access_type_vgather_release,
    access_type_vfetch,
    NUM_EXT_ACCESS_TYPES
};

#define MAX_VEC_SIZE_LOGBYTES 7
#define MAX_VEC_SIZE_BYTES  (1 << MAX_VEC_SIZE_LOGBYTES)

#define NUM_VREGS           32
#define NUM_QREGS           4



typedef uint32_t VRegMask; /* at least NUM_VREGS bits */
typedef uint32_t QRegMask; /* at least NUM_QREGS bits */

/* Use software vector length? */
#define VECTOR_SIZE_BYTE    (fVECSIZE())

typedef union {
    size8u_t ud[MAX_VEC_SIZE_BYTES / 8];
    size8s_t  d[MAX_VEC_SIZE_BYTES / 8];
    size4u_t uw[MAX_VEC_SIZE_BYTES / 4];
    size4s_t  w[MAX_VEC_SIZE_BYTES / 4];
    size2u_t uh[MAX_VEC_SIZE_BYTES / 2];
    size2s_t  h[MAX_VEC_SIZE_BYTES / 2];
    size1u_t ub[MAX_VEC_SIZE_BYTES / 1];
    size1s_t  b[MAX_VEC_SIZE_BYTES / 1];
} mmvector_t;

typedef union {
    size8u_t ud[2 * MAX_VEC_SIZE_BYTES / 8];
    size8s_t  d[2 * MAX_VEC_SIZE_BYTES / 8];
    size4u_t uw[2 * MAX_VEC_SIZE_BYTES / 4];
    size4s_t  w[2 * MAX_VEC_SIZE_BYTES / 4];
    size2u_t uh[2 * MAX_VEC_SIZE_BYTES / 2];
    size2s_t  h[2 * MAX_VEC_SIZE_BYTES / 2];
    size1u_t ub[2 * MAX_VEC_SIZE_BYTES / 1];
    size1s_t  b[2 * MAX_VEC_SIZE_BYTES / 1];
    mmvector_t v[2];
} mmvector_pair_t;

typedef union {
    size8u_t ud[MAX_VEC_SIZE_BYTES / 8 / 8];
    size8s_t  d[MAX_VEC_SIZE_BYTES / 8 / 8];
    size4u_t uw[MAX_VEC_SIZE_BYTES / 4 / 8];
    size4s_t  w[MAX_VEC_SIZE_BYTES / 4 / 8];
    size2u_t uh[MAX_VEC_SIZE_BYTES / 2 / 8];
    size2s_t  h[MAX_VEC_SIZE_BYTES / 2 / 8];
    size1u_t ub[MAX_VEC_SIZE_BYTES / 1 / 8];
    size1s_t  b[MAX_VEC_SIZE_BYTES / 1 / 8];
} mmqreg_t;

typedef struct {
    mmvector_t data;
    mmvector_t mask;
    mmvector_pair_t offsets;
    int size;
    vaddr_t va_base;
    vaddr_t va[MAX_VEC_SIZE_BYTES];
    int oob_access;
    int op;
    int op_size;
} vtcm_storelog_t;


static inline mmvector_t mmvec_zero_vector(void)
{
    mmvector_t ret;
    memset(&ret, 0, sizeof(ret));
    return ret;
}

static inline int mmvec_v2x_XA_adjust(int XA)
{
    return XA & 0x5;
}


enum {
    EXT_DFL,
    EXT_NEW,
    EXT_TMP
};



#ifdef QEMU_GENERATE
#define DECL_VREG(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_vreg_offset(ctx, 1); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_VREG_d(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_s(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_t(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_u(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_v(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_w(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_x(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_y(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)

#define DECL_VREG_PAIR(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_vreg_offset(ctx, 2); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_VREG_dd(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)
#define DECL_VREG_uu(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)
#define DECL_VREG_vv(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)
#define DECL_VREG_xx(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)

#define DECL_QREG(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_qreg_offset(ctx); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_QREG_d(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_e(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_s(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_t(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_u(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_v(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_x(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)

#define FREE_VREG(VAR)          tcg_temp_free_ptr(VAR)
#define FREE_VREG_d(VAR)        FREE_VREG(VAR)
#define FREE_VREG_s(VAR)        FREE_VREG(VAR)
#define FREE_VREG_u(VAR)        FREE_VREG(VAR)
#define FREE_VREG_v(VAR)        FREE_VREG(VAR)
#define FREE_VREG_w(VAR)        FREE_VREG(VAR)
#define FREE_VREG_x(VAR)        FREE_VREG(VAR)
#define FREE_VREG_y(VAR)        FREE_VREG(VAR)

#define FREE_VREG_PAIR(VAR)     tcg_temp_free_ptr(VAR)
#define FREE_VREG_dd(VAR)       FREE_VREG_PAIR(VAR)
#define FREE_VREG_uu(VAR)       FREE_VREG_PAIR(VAR)
#define FREE_VREG_vv(VAR)       FREE_VREG_PAIR(VAR)
#define FREE_VREG_xx(VAR)       FREE_VREG_PAIR(VAR)

#define FREE_QREG(VAR)          tcg_temp_free_ptr(VAR)
#define FREE_QREG_d(VAR)        FREE_QREG(VAR)
#define FREE_QREG_e(VAR)        FREE_QREG(VAR)
#define FREE_QREG_s(VAR)        FREE_QREG(VAR)
#define FREE_QREG_t(VAR)        FREE_QREG(VAR)
#define FREE_QREG_u(VAR)        FREE_QREG(VAR)
#define FREE_QREG_v(VAR)        FREE_QREG(VAR)
#define FREE_QREG_x(VAR)        FREE_QREG(VAR)

#define READ_VREG(VAR, NUM) \
    gen_read_vreg(VAR, NUM, 0)
#define READ_VREG_s(VAR, NUM)    READ_VREG(VAR, NUM)
#define READ_VREG_u(VAR, NUM)    READ_VREG(VAR, NUM)
#define READ_VREG_v(VAR, NUM)    READ_VREG(VAR, NUM)
#define READ_VREG_w(VAR, NUM)    READ_VREG(VAR, NUM)
#define READ_VREG_x(VAR, NUM)    READ_VREG(VAR, NUM)
#define READ_VREG_y(VAR, NUM)    READ_VREG(VAR, NUM)

#define READ_VREG_PAIR(VAR, NUM) \
    gen_read_vreg_pair(VAR, NUM, 0)
#define READ_VREG_uu(VAR, NUM)   READ_VREG_PAIR(VAR, NUM)
#define READ_VREG_vv(VAR, NUM)   READ_VREG_PAIR(VAR, NUM)
#define READ_VREG_xx(VAR, NUM)   READ_VREG_PAIR(VAR, NUM)

#define READ_QREG(VAR, NUM) \
    gen_read_qreg(VAR, NUM, 0)
#define READ_QREG_s(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_t(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_u(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_v(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_x(VAR, NUM)     READ_QREG(VAR, NUM)

#define DECL_NEW_OREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define READ_NEW_OREG(tmp, i) (tmp = tcg_const_tl(i))

#define FREE_NEW_OREG(NAME) \
    tcg_temp_free(NAME)

#define LOG_VREG_WRITE(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_vreg_write(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM), is_predicated); \
    } while (0)

#define LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_vreg_write_pair(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM) ^ 0, is_predicated); \
        ctx_log_vreg_write(ctx, (NUM) ^ 1, is_predicated); \
    } while (0)

#define LOG_QREG_WRITE(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_qreg_write(VAR, NUM, VNEW, insn->slot); \
        ctx_log_qreg_write(ctx, (NUM), is_predicated); \
    } while (0)
#else
#define NEW_WRITTEN(NUM) ((env->VRegs_select >> (NUM)) & 1)
#define TMP_WRITTEN(NUM) ((env->VRegs_updated_tmp >> (NUM)) & 1)

#define LOG_VREG_WRITE_FUNC(X) \
    _Generic((X), void * : log_ext_vreg_write, mmvector_t : log_mmvector_write)
#define LOG_VREG_WRITE(NUM, VAR, VNEW) \
    LOG_VREG_WRITE_FUNC(VAR)(env, NUM, VAR, VNEW, slot)

#define READ_EXT_VREG(NUM, VAR, VTMP) \
    do { \
        VAR = ((NEW_WRITTEN(NUM)) ? env->future_VRegs[NUM] \
                                  : env->VRegs[NUM]); \
        VAR = ((TMP_WRITTEN(NUM)) ? env->tmp_VRegs[NUM] : VAR); \
        if (VTMP == EXT_TMP) { \
            if (env->VRegs_updated & ((VRegMask)1) << (NUM)) { \
                VAR = env->future_VRegs[NUM]; \
                env->VRegs_updated ^= ((VRegMask)1) << (NUM); \
            } \
        } \
    } while (0)

#define READ_EXT_VREG_PAIR(NUM, VAR, VTMP) \
    do { \
        READ_EXT_VREG((NUM) ^ 0, VAR.v[0], VTMP); \
        READ_EXT_VREG((NUM) ^ 1, VAR.v[1], VTMP) \
    } while (0)
#endif

#define WRITE_EXT_VREG(NUM, VAR, VNEW)   LOG_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_VREG_d(NUM, VAR, VNEW)     LOG_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_VREG_x(NUM, VAR, VNEW)     LOG_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_VREG_y(NUM, VAR, VNEW)     LOG_VREG_WRITE(NUM, VAR, VNEW)

#define WRITE_VREG_dd(NUM, VAR, VNEW)    LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW)
#define WRITE_VREG_xx(NUM, VAR, VNEW)    LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW)
#define WRITE_VREG_yy(NUM, VAR, VNEW)    LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW)

#define WRITE_QREG_d(NUM, VAR, VNEW)     LOG_QREG_WRITE(NUM, VAR, VNEW)
#define WRITE_QREG_e(NUM, VAR, VNEW)     LOG_QREG_WRITE(NUM, VAR, VNEW)
#define WRITE_QREG_x(NUM, VAR, VNEW)     LOG_QREG_WRITE(NUM, VAR, VNEW)

#define LOG_VTCM_BYTE(VA, MASK, VAL, IDX) \
    do { \
        env->vtcm_log.data.ub[IDX] = (VAL); \
        env->vtcm_log.mask.ub[IDX] = (MASK); \
        env->vtcm_log.va[IDX] = (VA); \
    } while (0)

/* VTCM Banks */
#define LOG_VTCM_BANK(VAL, MASK, IDX) \
    do { \
        env->vtcm_log.offsets.uh[IDX]  = (VAL & 0xFFF); \
        env->vtcm_log.offsets.uh[IDX] |= ((MASK & 0xF) << 12) ; \
    } while (0)

void mem_load_vector_oddva(CPUHexagonState *env, vaddr_t vaddr,
                           vaddr_t lookup_vaddr, int slot, int size,
                           size1u_t *data, int use_full_va);
void mem_store_vector_oddva(CPUHexagonState *env, vaddr_t vaddr,
                            vaddr_t lookup_vaddr, int slot, int size,
                            size1u_t *data, size1u_t* mask, unsigned invert,
                            int use_full_va);
void mem_vector_scatter_init(CPUHexagonState *env, int slot, vaddr_t base_vaddr,
                             int length, int element_size);
void mem_vector_scatter_finish(CPUHexagonState *env, int slot, int op);
void mem_vector_gather_finish(CPUHexagonState *env, int slot);
void mem_vector_gather_init(CPUHexagonState *env, int slot, vaddr_t base_vaddr,
                            int length, int element_size);

/* Grabs the .tmp data, wherever it is, and clears the .tmp status */
/* Used for vhist */
static inline mmvector_t mmvec_vtmp_data(void)
{
    mmvector_t ret;
    g_assert_not_reached();
    return ret;
}

#endif

