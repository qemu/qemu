/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef _VEC_ARCH_H
#define _VEC_ARCH_H

#define VECEXT 1
#include "arch_types.h"
#include <stdint.h>
#include "max.h"
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
    size8u_t ud[4 * MAX_VEC_SIZE_BYTES / 8];
    size8s_t  d[4 * MAX_VEC_SIZE_BYTES / 8];
    size4u_t uw[4 * MAX_VEC_SIZE_BYTES / 4];
    size4s_t  w[4 * MAX_VEC_SIZE_BYTES / 4];
    size2u_t uh[4 * MAX_VEC_SIZE_BYTES / 2];
    size2s_t  h[4 * MAX_VEC_SIZE_BYTES / 2];
    size1u_t ub[4 * MAX_VEC_SIZE_BYTES / 1];
    size1s_t  b[4 * MAX_VEC_SIZE_BYTES / 1];
    mmvector_t v[4];
} mmvector_quad_t;

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
#define DECL_EXT_VREG(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_vreg_offset(ctx, 1); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_EXT_VREG_PAIR(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_vreg_offset(ctx, 2); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_EXT_VREG_QUAD(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_vreg_offset(ctx, 4); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_EXT_QREG(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_qreg_offset(ctx); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define FREE_EXT_VREG(VAR)          tcg_temp_free_ptr(VAR)
#define FREE_EXT_VREG_PAIR(VAR)     tcg_temp_free_ptr(VAR)
#define FREE_EXT_VREG_QUAD(VAR)     tcg_temp_free_ptr(VAR)
#define FREE_EXT_QREG(VAR)          tcg_temp_free_ptr(VAR)

#define READ_EXT_VREG(NUM, VAR, VTMP) \
    gen_read_ext_vreg(VAR, NUM, VTMP)

#define READ_EXT_VREG_PAIR(NUM, VAR, VTMP) \
    gen_read_ext_vreg_pair(VAR, NUM, VTMP)

#define READ_EXT_VREG_QUAD(NUM, VAR, VTMP) \
    gen_read_ext_vreg_quad(VAR, NUM, VTMP)

#define READ_EXT_QREG(NUM, VAR, VTMP) \
    gen_read_ext_qreg(VAR, NUM, VTMP)

#define DECL_NEW_OREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define READ_NEW_OREG(tmp, i) (tmp = tcg_const_tl(i))

#define FREE_NEW_OREG(NAME) \
    tcg_temp_free(NAME)

#define LOG_EXT_VREG_WRITE(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_vreg_write(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM), is_predicated); \
    } while (0)

#define LOG_EXT_VREG_WRITE_PAIR(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_vreg_write_pair(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM) ^ 0, is_predicated); \
        ctx_log_vreg_write(ctx, (NUM) ^ 1, is_predicated); \
    } while (0)

#define LOG_EXT_VREG_WRITE_QUAD(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_vreg_write_quad(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM) ^ 0, is_predicated); \
        ctx_log_vreg_write(ctx, (NUM) ^ 1, is_predicated); \
        ctx_log_vreg_write(ctx, (NUM) ^ 2, is_predicated); \
        ctx_log_vreg_write(ctx, (NUM) ^ 3, is_predicated); \
    } while (0)

#define LOG_EXT_QREG_WRITE(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_ext_qreg_write(VAR, NUM, VNEW, insn->slot); \
        ctx_log_qreg_write(ctx, (NUM), is_predicated); \
    } while (0)
#else
#define NEW_WRITTEN(NUM) ((env->VRegs_select >> (NUM)) & 1)
#define TMP_WRITTEN(NUM) ((env->VRegs_updated_tmp >> (NUM)) & 1)

#define LOG_EXT_VREG_WRITE_FUNC(X) \
    _Generic((X), void * : log_ext_vreg_write, mmvector_t : log_mmvector_write)
#define LOG_EXT_VREG_WRITE(NUM, VAR, VNEW) \
    LOG_EXT_VREG_WRITE_FUNC(VAR)(env, NUM, VAR, VNEW, slot)

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

#define READ_EXT_VREG_QUAD(NUM, VAR, VTMP) \
    do { \
        READ_EXT_VREG((NUM) ^ 0, VAR.v[0], VTMP); \
        READ_EXT_VREG((NUM) ^ 1, VAR.v[1], VTMP); \
        READ_EXT_VREG((NUM) ^ 2, VAR.v[2], VTMP); \
        READ_EXT_VREG((NUM) ^ 3, VAR.v[3], VTMP); \
    } while (0)
#endif

#define WRITE_EXT_VREG(NUM, VAR, VNEW) \
    LOG_EXT_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_EXT_VREG_PAIR(NUM, VAR, VNEW) \
    LOG_EXT_VREG_WRITE_PAIR(NUM, VAR, VNEW)
#define WRITE_EXT_VREG_QUAD(NUM, VAR, VNEW) \
    LOG_EXT_VREG_WRITE_QUAD(NUM, VAR, VNEW)
#define WRITE_EXT_QREG(NUM, VAR, VNEW) \
    LOG_EXT_QREG_WRITE(NUM, VAR, VNEW)

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

#define READ_ZREG(NUM) \
    ({ DECL_EXT_ZREG(__tmpVR); READ_EXT_ZREG(NUM, __tmpVR, 0); __tmpVR; })
#define READ_VREG(NUM) \
    ({ DECL_EXT_VREG(__tmpVR); READ_EXT_VREG(NUM, __tmpVR, 0); __tmpVR; })
#define WRITE_VREG(NUM, VAR) \
    WRITE_EXT_VREG(NUM, VAR, EXT_DFL)

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

