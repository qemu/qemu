/*
 * ARM NEON vector operations.
 *
 * Copyright (c) 2007, 2008 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
#include "vec_internal.h"

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

#define SET_QC() env->vfp.qc[0] = 1

#define NEON_TYPE1(name, type) \
typedef struct \
{ \
    type v1; \
} neon_##name;
#if HOST_BIG_ENDIAN
#define NEON_TYPE2(name, type) \
typedef struct \
{ \
    type v2; \
    type v1; \
} neon_##name;
#define NEON_TYPE4(name, type) \
typedef struct \
{ \
    type v4; \
    type v3; \
    type v2; \
    type v1; \
} neon_##name;
#else
#define NEON_TYPE2(name, type) \
typedef struct \
{ \
    type v1; \
    type v2; \
} neon_##name;
#define NEON_TYPE4(name, type) \
typedef struct \
{ \
    type v1; \
    type v2; \
    type v3; \
    type v4; \
} neon_##name;
#endif

NEON_TYPE4(s8, int8_t)
NEON_TYPE4(u8, uint8_t)
NEON_TYPE2(s16, int16_t)
NEON_TYPE2(u16, uint16_t)
NEON_TYPE1(s32, int32_t)
NEON_TYPE1(u32, uint32_t)
#undef NEON_TYPE4
#undef NEON_TYPE2
#undef NEON_TYPE1

/* Copy from a uint32_t to a vector structure type.  */
#define NEON_UNPACK(vtype, dest, val) do { \
    union { \
        vtype v; \
        uint32_t i; \
    } conv_u; \
    conv_u.i = (val); \
    dest = conv_u.v; \
    } while(0)

/* Copy from a vector structure type to a uint32_t.  */
#define NEON_PACK(vtype, dest, val) do { \
    union { \
        vtype v; \
        uint32_t i; \
    } conv_u; \
    conv_u.v = (val); \
    dest = conv_u.i; \
    } while(0)

#define NEON_DO1 \
    NEON_FN(vdest.v1, vsrc1.v1, vsrc2.v1);
#define NEON_DO2 \
    NEON_FN(vdest.v1, vsrc1.v1, vsrc2.v1); \
    NEON_FN(vdest.v2, vsrc1.v2, vsrc2.v2);
#define NEON_DO4 \
    NEON_FN(vdest.v1, vsrc1.v1, vsrc2.v1); \
    NEON_FN(vdest.v2, vsrc1.v2, vsrc2.v2); \
    NEON_FN(vdest.v3, vsrc1.v3, vsrc2.v3); \
    NEON_FN(vdest.v4, vsrc1.v4, vsrc2.v4);

#define NEON_VOP_BODY(vtype, n) \
{ \
    uint32_t res; \
    vtype vsrc1; \
    vtype vsrc2; \
    vtype vdest; \
    NEON_UNPACK(vtype, vsrc1, arg1); \
    NEON_UNPACK(vtype, vsrc2, arg2); \
    NEON_DO##n; \
    NEON_PACK(vtype, res, vdest); \
    return res; \
}

#define NEON_VOP(name, vtype, n) \
uint32_t HELPER(glue(neon_,name))(uint32_t arg1, uint32_t arg2) \
NEON_VOP_BODY(vtype, n)

#define NEON_VOP_ENV(name, vtype, n) \
uint32_t HELPER(glue(neon_,name))(CPUARMState *env, uint32_t arg1, uint32_t arg2) \
NEON_VOP_BODY(vtype, n)

#define NEON_GVEC_VOP2(name, vtype) \
void HELPER(name)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    vtype *d = vd, *n = vn, *m = vm;                            \
    for (i = 0; i < opr_sz / sizeof(vtype); i++) {              \
        NEON_FN(d[i], n[i], m[i]);                              \
    }                                                           \
    clear_tail(d, opr_sz, simd_maxsz(desc));                    \
}

#define NEON_GVEC_VOP2_ENV(name, vtype) \
void HELPER(name)(void *vd, void *vn, void *vm, void *venv, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    vtype *d = vd, *n = vn, *m = vm;                            \
    CPUARMState *env = venv;                                    \
    for (i = 0; i < opr_sz / sizeof(vtype); i++) {              \
        NEON_FN(d[i], n[i], m[i]);                              \
    }                                                           \
    clear_tail(d, opr_sz, simd_maxsz(desc));                    \
}

#define NEON_GVEC_VOP2i_ENV(name, vtype) \
void HELPER(name)(void *vd, void *vn, void *venv, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    int imm = simd_data(desc);                                  \
    vtype *d = vd, *n = vn;                                     \
    CPUARMState *env = venv;                                    \
    for (i = 0; i < opr_sz / sizeof(vtype); i++) {              \
        NEON_FN(d[i], n[i], imm);                               \
    }                                                           \
    clear_tail(d, opr_sz, simd_maxsz(desc));                    \
}

/* Pairwise operations.  */
/* For 32-bit elements each segment only contains a single element, so
   the elementwise and pairwise operations are the same.  */
#define NEON_PDO2 \
    NEON_FN(vdest.v1, vsrc1.v1, vsrc1.v2); \
    NEON_FN(vdest.v2, vsrc2.v1, vsrc2.v2);
#define NEON_PDO4 \
    NEON_FN(vdest.v1, vsrc1.v1, vsrc1.v2); \
    NEON_FN(vdest.v2, vsrc1.v3, vsrc1.v4); \
    NEON_FN(vdest.v3, vsrc2.v1, vsrc2.v2); \
    NEON_FN(vdest.v4, vsrc2.v3, vsrc2.v4); \

#define NEON_POP(name, vtype, n) \
uint32_t HELPER(glue(neon_,name))(uint32_t arg1, uint32_t arg2) \
{ \
    uint32_t res; \
    vtype vsrc1; \
    vtype vsrc2; \
    vtype vdest; \
    NEON_UNPACK(vtype, vsrc1, arg1); \
    NEON_UNPACK(vtype, vsrc2, arg2); \
    NEON_PDO##n; \
    NEON_PACK(vtype, res, vdest); \
    return res; \
}

/* Unary operators.  */
#define NEON_VOP1(name, vtype, n) \
uint32_t HELPER(glue(neon_,name))(uint32_t arg) \
{ \
    vtype vsrc1; \
    vtype vdest; \
    NEON_UNPACK(vtype, vsrc1, arg); \
    NEON_DO##n; \
    NEON_PACK(vtype, arg, vdest); \
    return arg; \
}

#define NEON_FN(dest, src1, src2) dest = (src1 < src2) ? src1 : src2
NEON_POP(pmin_s8, neon_s8, 4)
NEON_POP(pmin_u8, neon_u8, 4)
NEON_POP(pmin_s16, neon_s16, 2)
NEON_POP(pmin_u16, neon_u16, 2)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) dest = (src1 > src2) ? src1 : src2
NEON_POP(pmax_s8, neon_s8, 4)
NEON_POP(pmax_u8, neon_u8, 4)
NEON_POP(pmax_s16, neon_s16, 2)
NEON_POP(pmax_u16, neon_u16, 2)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 16, false, NULL))
NEON_VOP(shl_u16, neon_u16, 2)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 16, false, NULL))
NEON_VOP(shl_s16, neon_s16, 2)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 8, true, NULL))
NEON_VOP(rshl_s8, neon_s8, 4)
NEON_GVEC_VOP2(gvec_srshl_b, int8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 16, true, NULL))
NEON_VOP(rshl_s16, neon_s16, 2)
NEON_GVEC_VOP2(gvec_srshl_h, int16_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 32, true, NULL))
NEON_GVEC_VOP2(gvec_srshl_s, int32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_d(src1, (int8_t)src2, true, NULL))
NEON_GVEC_VOP2(gvec_srshl_d, int64_t)
#undef NEON_FN

uint32_t HELPER(neon_rshl_s32)(uint32_t val, uint32_t shift)
{
    return do_sqrshl_bhs(val, (int8_t)shift, 32, true, NULL);
}

uint64_t HELPER(neon_rshl_s64)(uint64_t val, uint64_t shift)
{
    return do_sqrshl_d(val, (int8_t)shift, true, NULL);
}

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 8, true, NULL))
NEON_VOP(rshl_u8, neon_u8, 4)
NEON_GVEC_VOP2(gvec_urshl_b, uint8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 16, true, NULL))
NEON_VOP(rshl_u16, neon_u16, 2)
NEON_GVEC_VOP2(gvec_urshl_h, uint16_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 32, true, NULL))
NEON_GVEC_VOP2(gvec_urshl_s, int32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_d(src1, (int8_t)src2, true, NULL))
NEON_GVEC_VOP2(gvec_urshl_d, int64_t)
#undef NEON_FN

uint32_t HELPER(neon_rshl_u32)(uint32_t val, uint32_t shift)
{
    return do_uqrshl_bhs(val, (int8_t)shift, 32, true, NULL);
}

uint64_t HELPER(neon_rshl_u64)(uint64_t val, uint64_t shift)
{
    return do_uqrshl_d(val, (int8_t)shift, true, NULL);
}

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 8, false, env->vfp.qc))
NEON_VOP_ENV(qshl_u8, neon_u8, 4)
NEON_GVEC_VOP2_ENV(neon_uqshl_b, uint8_t)
NEON_GVEC_VOP2i_ENV(neon_uqshli_b, uint8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 16, false, env->vfp.qc))
NEON_VOP_ENV(qshl_u16, neon_u16, 2)
NEON_GVEC_VOP2_ENV(neon_uqshl_h, uint16_t)
NEON_GVEC_VOP2i_ENV(neon_uqshli_h, uint16_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 32, false, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_uqshl_s, uint32_t)
NEON_GVEC_VOP2i_ENV(neon_uqshli_s, uint32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_d(src1, (int8_t)src2, false, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_uqshl_d, uint64_t)
NEON_GVEC_VOP2i_ENV(neon_uqshli_d, uint64_t)
#undef NEON_FN

uint32_t HELPER(neon_qshl_u32)(CPUARMState *env, uint32_t val, uint32_t shift)
{
    return do_uqrshl_bhs(val, (int8_t)shift, 32, false, env->vfp.qc);
}

uint64_t HELPER(neon_qshl_u64)(CPUARMState *env, uint64_t val, uint64_t shift)
{
    return do_uqrshl_d(val, (int8_t)shift, false, env->vfp.qc);
}

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 8, false, env->vfp.qc))
NEON_VOP_ENV(qshl_s8, neon_s8, 4)
NEON_GVEC_VOP2_ENV(neon_sqshl_b, int8_t)
NEON_GVEC_VOP2i_ENV(neon_sqshli_b, int8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 16, false, env->vfp.qc))
NEON_VOP_ENV(qshl_s16, neon_s16, 2)
NEON_GVEC_VOP2_ENV(neon_sqshl_h, int16_t)
NEON_GVEC_VOP2i_ENV(neon_sqshli_h, int16_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 32, false, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_sqshl_s, int32_t)
NEON_GVEC_VOP2i_ENV(neon_sqshli_s, int32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_d(src1, (int8_t)src2, false, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_sqshl_d, int64_t)
NEON_GVEC_VOP2i_ENV(neon_sqshli_d, int64_t)
#undef NEON_FN

uint32_t HELPER(neon_qshl_s32)(CPUARMState *env, uint32_t val, uint32_t shift)
{
    return do_sqrshl_bhs(val, (int8_t)shift, 32, false, env->vfp.qc);
}

uint64_t HELPER(neon_qshl_s64)(CPUARMState *env, uint64_t val, uint64_t shift)
{
    return do_sqrshl_d(val, (int8_t)shift, false, env->vfp.qc);
}

#define NEON_FN(dest, src1, src2) \
    (dest = do_suqrshl_bhs(src1, (int8_t)src2, 8, false, env->vfp.qc))
NEON_VOP_ENV(qshlu_s8, neon_s8, 4)
NEON_GVEC_VOP2i_ENV(neon_sqshlui_b, int8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_suqrshl_bhs(src1, (int8_t)src2, 16, false, env->vfp.qc))
NEON_VOP_ENV(qshlu_s16, neon_s16, 2)
NEON_GVEC_VOP2i_ENV(neon_sqshlui_h, int16_t)
#undef NEON_FN

uint32_t HELPER(neon_qshlu_s32)(CPUARMState *env, uint32_t val, uint32_t shift)
{
    return do_suqrshl_bhs(val, (int8_t)shift, 32, false, env->vfp.qc);
}

uint64_t HELPER(neon_qshlu_s64)(CPUARMState *env, uint64_t val, uint64_t shift)
{
    return do_suqrshl_d(val, (int8_t)shift, false, env->vfp.qc);
}

#define NEON_FN(dest, src1, src2) \
    (dest = do_suqrshl_bhs(src1, (int8_t)src2, 32, false, env->vfp.qc))
NEON_GVEC_VOP2i_ENV(neon_sqshlui_s, int32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_suqrshl_d(src1, (int8_t)src2, false, env->vfp.qc))
NEON_GVEC_VOP2i_ENV(neon_sqshlui_d, int64_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 8, true, env->vfp.qc))
NEON_VOP_ENV(qrshl_u8, neon_u8, 4)
NEON_GVEC_VOP2_ENV(neon_uqrshl_b, uint8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 16, true, env->vfp.qc))
NEON_VOP_ENV(qrshl_u16, neon_u16, 2)
NEON_GVEC_VOP2_ENV(neon_uqrshl_h, uint16_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_bhs(src1, (int8_t)src2, 32, true, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_uqrshl_s, uint32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_uqrshl_d(src1, (int8_t)src2, true, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_uqrshl_d, uint64_t)
#undef NEON_FN

uint32_t HELPER(neon_qrshl_u32)(CPUARMState *env, uint32_t val, uint32_t shift)
{
    return do_uqrshl_bhs(val, (int8_t)shift, 32, true, env->vfp.qc);
}

uint64_t HELPER(neon_qrshl_u64)(CPUARMState *env, uint64_t val, uint64_t shift)
{
    return do_uqrshl_d(val, (int8_t)shift, true, env->vfp.qc);
}

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 8, true, env->vfp.qc))
NEON_VOP_ENV(qrshl_s8, neon_s8, 4)
NEON_GVEC_VOP2_ENV(neon_sqrshl_b, int8_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 16, true, env->vfp.qc))
NEON_VOP_ENV(qrshl_s16, neon_s16, 2)
NEON_GVEC_VOP2_ENV(neon_sqrshl_h, int16_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_bhs(src1, (int8_t)src2, 32, true, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_sqrshl_s, int32_t)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) \
    (dest = do_sqrshl_d(src1, (int8_t)src2, true, env->vfp.qc))
NEON_GVEC_VOP2_ENV(neon_sqrshl_d, int64_t)
#undef NEON_FN

uint32_t HELPER(neon_qrshl_s32)(CPUARMState *env, uint32_t val, uint32_t shift)
{
    return do_sqrshl_bhs(val, (int8_t)shift, 32, true, env->vfp.qc);
}

uint64_t HELPER(neon_qrshl_s64)(CPUARMState *env, uint64_t val, uint64_t shift)
{
    return do_sqrshl_d(val, (int8_t)shift, true, env->vfp.qc);
}

uint32_t HELPER(neon_add_u8)(uint32_t a, uint32_t b)
{
    uint32_t mask;
    mask = (a ^ b) & 0x80808080u;
    a &= ~0x80808080u;
    b &= ~0x80808080u;
    return (a + b) ^ mask;
}

uint32_t HELPER(neon_add_u16)(uint32_t a, uint32_t b)
{
    uint32_t mask;
    mask = (a ^ b) & 0x80008000u;
    a &= ~0x80008000u;
    b &= ~0x80008000u;
    return (a + b) ^ mask;
}

#define NEON_FN(dest, src1, src2) dest = src1 - src2
NEON_VOP(sub_u8, neon_u8, 4)
NEON_VOP(sub_u16, neon_u16, 2)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) dest = src1 * src2
NEON_VOP(mul_u8, neon_u8, 4)
NEON_VOP(mul_u16, neon_u16, 2)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) dest = (src1 & src2) ? -1 : 0
NEON_VOP(tst_u8, neon_u8, 4)
NEON_VOP(tst_u16, neon_u16, 2)
NEON_VOP(tst_u32, neon_u32, 1)
#undef NEON_FN

/* Count Leading Sign/Zero Bits.  */
static inline int do_clz8(uint8_t x)
{
    int n;
    for (n = 8; x; n--)
        x >>= 1;
    return n;
}

static inline int do_clz16(uint16_t x)
{
    int n;
    for (n = 16; x; n--)
        x >>= 1;
    return n;
}

#define NEON_FN(dest, src, dummy) dest = do_clz8(src)
NEON_VOP1(clz_u8, neon_u8, 4)
#undef NEON_FN

#define NEON_FN(dest, src, dummy) dest = do_clz16(src)
NEON_VOP1(clz_u16, neon_u16, 2)
#undef NEON_FN

#define NEON_FN(dest, src, dummy) dest = do_clz8((src < 0) ? ~src : src) - 1
NEON_VOP1(cls_s8, neon_s8, 4)
#undef NEON_FN

#define NEON_FN(dest, src, dummy) dest = do_clz16((src < 0) ? ~src : src) - 1
NEON_VOP1(cls_s16, neon_s16, 2)
#undef NEON_FN

uint32_t HELPER(neon_cls_s32)(uint32_t x)
{
    int count;
    if ((int32_t)x < 0)
        x = ~x;
    for (count = 32; x; count--)
        x = x >> 1;
    return count - 1;
}

/* Bit count.  */
uint32_t HELPER(neon_cnt_u8)(uint32_t x)
{
    x = (x & 0x55555555) + ((x >>  1) & 0x55555555);
    x = (x & 0x33333333) + ((x >>  2) & 0x33333333);
    x = (x & 0x0f0f0f0f) + ((x >>  4) & 0x0f0f0f0f);
    return x;
}

/* Reverse bits in each 8 bit word */
uint32_t HELPER(neon_rbit_u8)(uint32_t x)
{
    x =  ((x & 0xf0f0f0f0) >> 4)
       | ((x & 0x0f0f0f0f) << 4);
    x =  ((x & 0x88888888) >> 3)
       | ((x & 0x44444444) >> 1)
       | ((x & 0x22222222) << 1)
       | ((x & 0x11111111) << 3);
    return x;
}

#define NEON_QDMULH16(dest, src1, src2, round) do { \
    uint32_t tmp = (int32_t)(int16_t) src1 * (int16_t) src2; \
    if ((tmp ^ (tmp << 1)) & SIGNBIT) { \
        SET_QC(); \
        tmp = (tmp >> 31) ^ ~SIGNBIT; \
    } else { \
        tmp <<= 1; \
    } \
    if (round) { \
        int32_t old = tmp; \
        tmp += 1 << 15; \
        if ((int32_t)tmp < old) { \
            SET_QC(); \
            tmp = SIGNBIT - 1; \
        } \
    } \
    dest = tmp >> 16; \
    } while(0)
#define NEON_FN(dest, src1, src2) NEON_QDMULH16(dest, src1, src2, 0)
NEON_VOP_ENV(qdmulh_s16, neon_s16, 2)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_QDMULH16(dest, src1, src2, 1)
NEON_VOP_ENV(qrdmulh_s16, neon_s16, 2)
#undef NEON_FN
#undef NEON_QDMULH16

#define NEON_QDMULH32(dest, src1, src2, round) do { \
    uint64_t tmp = (int64_t)(int32_t) src1 * (int32_t) src2; \
    if ((tmp ^ (tmp << 1)) & SIGNBIT64) { \
        SET_QC(); \
        tmp = (tmp >> 63) ^ ~SIGNBIT64; \
    } else { \
        tmp <<= 1; \
    } \
    if (round) { \
        int64_t old = tmp; \
        tmp += (int64_t)1 << 31; \
        if ((int64_t)tmp < old) { \
            SET_QC(); \
            tmp = SIGNBIT64 - 1; \
        } \
    } \
    dest = tmp >> 32; \
    } while(0)
#define NEON_FN(dest, src1, src2) NEON_QDMULH32(dest, src1, src2, 0)
NEON_VOP_ENV(qdmulh_s32, neon_s32, 1)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_QDMULH32(dest, src1, src2, 1)
NEON_VOP_ENV(qrdmulh_s32, neon_s32, 1)
#undef NEON_FN
#undef NEON_QDMULH32

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_u8)(uint64_t x)
{
    return (x & 0xffu) | ((x >> 8) & 0xff00u) | ((x >> 16) & 0xff0000u)
           | ((x >> 24) & 0xff000000u);
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_u16)(uint64_t x)
{
    return (x & 0xffffu) | ((x >> 16) & 0xffff0000u);
}

uint32_t HELPER(neon_narrow_high_u8)(uint64_t x)
{
    return ((x >> 8) & 0xff) | ((x >> 16) & 0xff00)
            | ((x >> 24) & 0xff0000) | ((x >> 32) & 0xff000000);
}

uint32_t HELPER(neon_narrow_high_u16)(uint64_t x)
{
    return ((x >> 16) & 0xffff) | ((x >> 32) & 0xffff0000);
}

uint32_t HELPER(neon_narrow_round_high_u8)(uint64_t x)
{
    x &= 0xff80ff80ff80ff80ull;
    x += 0x0080008000800080ull;
    return ((x >> 8) & 0xff) | ((x >> 16) & 0xff00)
            | ((x >> 24) & 0xff0000) | ((x >> 32) & 0xff000000);
}

uint32_t HELPER(neon_narrow_round_high_u16)(uint64_t x)
{
    x &= 0xffff8000ffff8000ull;
    x += 0x0000800000008000ull;
    return ((x >> 16) & 0xffff) | ((x >> 32) & 0xffff0000);
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_unarrow_sat8)(CPUARMState *env, uint64_t x)
{
    uint16_t s;
    uint8_t d;
    uint32_t res = 0;
#define SAT8(n) \
    s = x >> n; \
    if (s & 0x8000) { \
        SET_QC(); \
    } else { \
        if (s > 0xff) { \
            d = 0xff; \
            SET_QC(); \
        } else  { \
            d = s; \
        } \
        res |= (uint32_t)d << (n / 2); \
    }

    SAT8(0);
    SAT8(16);
    SAT8(32);
    SAT8(48);
#undef SAT8
    return res;
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_sat_u8)(CPUARMState *env, uint64_t x)
{
    uint16_t s;
    uint8_t d;
    uint32_t res = 0;
#define SAT8(n) \
    s = x >> n; \
    if (s > 0xff) { \
        d = 0xff; \
        SET_QC(); \
    } else  { \
        d = s; \
    } \
    res |= (uint32_t)d << (n / 2);

    SAT8(0);
    SAT8(16);
    SAT8(32);
    SAT8(48);
#undef SAT8
    return res;
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_sat_s8)(CPUARMState *env, uint64_t x)
{
    int16_t s;
    uint8_t d;
    uint32_t res = 0;
#define SAT8(n) \
    s = x >> n; \
    if (s != (int8_t)s) { \
        d = (s >> 15) ^ 0x7f; \
        SET_QC(); \
    } else  { \
        d = s; \
    } \
    res |= (uint32_t)d << (n / 2);

    SAT8(0);
    SAT8(16);
    SAT8(32);
    SAT8(48);
#undef SAT8
    return res;
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_unarrow_sat16)(CPUARMState *env, uint64_t x)
{
    uint32_t high;
    uint32_t low;
    low = x;
    if (low & 0x80000000) {
        low = 0;
        SET_QC();
    } else if (low > 0xffff) {
        low = 0xffff;
        SET_QC();
    }
    high = x >> 32;
    if (high & 0x80000000) {
        high = 0;
        SET_QC();
    } else if (high > 0xffff) {
        high = 0xffff;
        SET_QC();
    }
    return deposit32(low, 16, 16, high);
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_sat_u16)(CPUARMState *env, uint64_t x)
{
    uint32_t high;
    uint32_t low;
    low = x;
    if (low > 0xffff) {
        low = 0xffff;
        SET_QC();
    }
    high = x >> 32;
    if (high > 0xffff) {
        high = 0xffff;
        SET_QC();
    }
    return deposit32(low, 16, 16, high);
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_sat_s16)(CPUARMState *env, uint64_t x)
{
    int32_t low;
    int32_t high;
    low = x;
    if (low != (int16_t)low) {
        low = (low >> 31) ^ 0x7fff;
        SET_QC();
    }
    high = x >> 32;
    if (high != (int16_t)high) {
        high = (high >> 31) ^ 0x7fff;
        SET_QC();
    }
    return deposit32(low, 16, 16, high);
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_unarrow_sat32)(CPUARMState *env, uint64_t x)
{
    if (x & 0x8000000000000000ull) {
        SET_QC();
        return 0;
    }
    if (x > 0xffffffffu) {
        SET_QC();
        return 0xffffffffu;
    }
    return x;
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_sat_u32)(CPUARMState *env, uint64_t x)
{
    if (x > 0xffffffffu) {
        SET_QC();
        return 0xffffffffu;
    }
    return x;
}

/* Only the low 32-bits of output are significant. */
uint64_t HELPER(neon_narrow_sat_s32)(CPUARMState *env, uint64_t x)
{
    if ((int64_t)x != (int32_t)x) {
        SET_QC();
        return (uint32_t)((int64_t)x >> 63) ^ 0x7fffffff;
    }
    return (uint32_t)x;
}

uint64_t HELPER(neon_widen_u8)(uint32_t x)
{
    uint64_t tmp;
    uint64_t ret;
    ret = (uint8_t)x;
    tmp = (uint8_t)(x >> 8);
    ret |= tmp << 16;
    tmp = (uint8_t)(x >> 16);
    ret |= tmp << 32;
    tmp = (uint8_t)(x >> 24);
    ret |= tmp << 48;
    return ret;
}

uint64_t HELPER(neon_widen_s8)(uint32_t x)
{
    uint64_t tmp;
    uint64_t ret;
    ret = (uint16_t)(int8_t)x;
    tmp = (uint16_t)(int8_t)(x >> 8);
    ret |= tmp << 16;
    tmp = (uint16_t)(int8_t)(x >> 16);
    ret |= tmp << 32;
    tmp = (uint16_t)(int8_t)(x >> 24);
    ret |= tmp << 48;
    return ret;
}

uint64_t HELPER(neon_widen_u16)(uint32_t x)
{
    uint64_t high = (uint16_t)(x >> 16);
    return ((uint16_t)x) | (high << 32);
}

uint64_t HELPER(neon_widen_s16)(uint32_t x)
{
    uint64_t high = (int16_t)(x >> 16);
    return ((uint32_t)(int16_t)x) | (high << 32);
}

uint64_t HELPER(neon_addl_u16)(uint64_t a, uint64_t b)
{
    uint64_t mask;
    mask = (a ^ b) & 0x8000800080008000ull;
    a &= ~0x8000800080008000ull;
    b &= ~0x8000800080008000ull;
    return (a + b) ^ mask;
}

uint64_t HELPER(neon_addl_u32)(uint64_t a, uint64_t b)
{
    uint64_t mask;
    mask = (a ^ b) & 0x8000000080000000ull;
    a &= ~0x8000000080000000ull;
    b &= ~0x8000000080000000ull;
    return (a + b) ^ mask;
}

uint64_t HELPER(neon_paddl_u16)(uint64_t a, uint64_t b)
{
    uint64_t tmp;
    uint64_t tmp2;

    tmp = a & 0x0000ffff0000ffffull;
    tmp += (a >> 16) & 0x0000ffff0000ffffull;
    tmp2 = b & 0xffff0000ffff0000ull;
    tmp2 += (b << 16) & 0xffff0000ffff0000ull;
    return    ( tmp         & 0xffff)
            | ((tmp  >> 16) & 0xffff0000ull)
            | ((tmp2 << 16) & 0xffff00000000ull)
            | ( tmp2        & 0xffff000000000000ull);
}

uint64_t HELPER(neon_paddl_u32)(uint64_t a, uint64_t b)
{
    uint32_t low = a + (a >> 32);
    uint32_t high = b + (b >> 32);
    return low + ((uint64_t)high << 32);
}

uint64_t HELPER(neon_subl_u16)(uint64_t a, uint64_t b)
{
    uint64_t mask;
    mask = (a ^ ~b) & 0x8000800080008000ull;
    a |= 0x8000800080008000ull;
    b &= ~0x8000800080008000ull;
    return (a - b) ^ mask;
}

uint64_t HELPER(neon_subl_u32)(uint64_t a, uint64_t b)
{
    uint64_t mask;
    mask = (a ^ ~b) & 0x8000000080000000ull;
    a |= 0x8000000080000000ull;
    b &= ~0x8000000080000000ull;
    return (a - b) ^ mask;
}

uint64_t HELPER(neon_addl_saturate_s32)(CPUARMState *env, uint64_t a, uint64_t b)
{
    uint32_t x, y;
    uint32_t low, high;

    x = a;
    y = b;
    low = x + y;
    if (((low ^ x) & SIGNBIT) && !((x ^ y) & SIGNBIT)) {
        SET_QC();
        low = ((int32_t)x >> 31) ^ ~SIGNBIT;
    }
    x = a >> 32;
    y = b >> 32;
    high = x + y;
    if (((high ^ x) & SIGNBIT) && !((x ^ y) & SIGNBIT)) {
        SET_QC();
        high = ((int32_t)x >> 31) ^ ~SIGNBIT;
    }
    return low | ((uint64_t)high << 32);
}

uint64_t HELPER(neon_addl_saturate_s64)(CPUARMState *env, uint64_t a, uint64_t b)
{
    uint64_t result;

    result = a + b;
    if (((result ^ a) & SIGNBIT64) && !((a ^ b) & SIGNBIT64)) {
        SET_QC();
        result = ((int64_t)a >> 63) ^ ~SIGNBIT64;
    }
    return result;
}

/* We have to do the arithmetic in a larger type than
 * the input type, because for example with a signed 32 bit
 * op the absolute difference can overflow a signed 32 bit value.
 */
#define DO_ABD(dest, x, y, intype, arithtype) do {            \
    arithtype tmp_x = (intype)(x);                            \
    arithtype tmp_y = (intype)(y);                            \
    dest = ((tmp_x > tmp_y) ? tmp_x - tmp_y : tmp_y - tmp_x); \
    } while(0)

uint64_t HELPER(neon_abdl_u16)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;
    DO_ABD(result, a, b, uint8_t, uint32_t);
    DO_ABD(tmp, a >> 8, b >> 8, uint8_t, uint32_t);
    result |= tmp << 16;
    DO_ABD(tmp, a >> 16, b >> 16, uint8_t, uint32_t);
    result |= tmp << 32;
    DO_ABD(tmp, a >> 24, b >> 24, uint8_t, uint32_t);
    result |= tmp << 48;
    return result;
}

uint64_t HELPER(neon_abdl_s16)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;
    DO_ABD(result, a, b, int8_t, int32_t);
    DO_ABD(tmp, a >> 8, b >> 8, int8_t, int32_t);
    result |= tmp << 16;
    DO_ABD(tmp, a >> 16, b >> 16, int8_t, int32_t);
    result |= tmp << 32;
    DO_ABD(tmp, a >> 24, b >> 24, int8_t, int32_t);
    result |= tmp << 48;
    return result;
}

uint64_t HELPER(neon_abdl_u32)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;
    DO_ABD(result, a, b, uint16_t, uint32_t);
    DO_ABD(tmp, a >> 16, b >> 16, uint16_t, uint32_t);
    return result | (tmp << 32);
}

uint64_t HELPER(neon_abdl_s32)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;
    DO_ABD(result, a, b, int16_t, int32_t);
    DO_ABD(tmp, a >> 16, b >> 16, int16_t, int32_t);
    return result | (tmp << 32);
}

uint64_t HELPER(neon_abdl_u64)(uint32_t a, uint32_t b)
{
    uint64_t result;
    DO_ABD(result, a, b, uint32_t, uint64_t);
    return result;
}

uint64_t HELPER(neon_abdl_s64)(uint32_t a, uint32_t b)
{
    uint64_t result;
    DO_ABD(result, a, b, int32_t, int64_t);
    return result;
}
#undef DO_ABD

/* Widening multiply. Named type is the source type.  */
#define DO_MULL(dest, x, y, type1, type2) do { \
    type1 tmp_x = x; \
    type1 tmp_y = y; \
    dest = (type2)((type2)tmp_x * (type2)tmp_y); \
    } while(0)

uint64_t HELPER(neon_mull_u8)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;

    DO_MULL(result, a, b, uint8_t, uint16_t);
    DO_MULL(tmp, a >> 8, b >> 8, uint8_t, uint16_t);
    result |= tmp << 16;
    DO_MULL(tmp, a >> 16, b >> 16, uint8_t, uint16_t);
    result |= tmp << 32;
    DO_MULL(tmp, a >> 24, b >> 24, uint8_t, uint16_t);
    result |= tmp << 48;
    return result;
}

uint64_t HELPER(neon_mull_s8)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;

    DO_MULL(result, a, b, int8_t, uint16_t);
    DO_MULL(tmp, a >> 8, b >> 8, int8_t, uint16_t);
    result |= tmp << 16;
    DO_MULL(tmp, a >> 16, b >> 16, int8_t, uint16_t);
    result |= tmp << 32;
    DO_MULL(tmp, a >> 24, b >> 24, int8_t, uint16_t);
    result |= tmp << 48;
    return result;
}

uint64_t HELPER(neon_mull_u16)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;

    DO_MULL(result, a, b, uint16_t, uint32_t);
    DO_MULL(tmp, a >> 16, b >> 16, uint16_t, uint32_t);
    return result | (tmp << 32);
}

uint64_t HELPER(neon_mull_s16)(uint32_t a, uint32_t b)
{
    uint64_t tmp;
    uint64_t result;

    DO_MULL(result, a, b, int16_t, uint32_t);
    DO_MULL(tmp, a >> 16, b >> 16, int16_t, uint32_t);
    return result | (tmp << 32);
}

uint64_t HELPER(neon_negl_u16)(uint64_t x)
{
    uint16_t tmp;
    uint64_t result;
    result = (uint16_t)-x;
    tmp = -(x >> 16);
    result |= (uint64_t)tmp << 16;
    tmp = -(x >> 32);
    result |= (uint64_t)tmp << 32;
    tmp = -(x >> 48);
    result |= (uint64_t)tmp << 48;
    return result;
}

uint64_t HELPER(neon_negl_u32)(uint64_t x)
{
    uint32_t low = -x;
    uint32_t high = -(x >> 32);
    return low | ((uint64_t)high << 32);
}

/* Saturating sign manipulation.  */
/* ??? Make these use NEON_VOP1 */
#define DO_QABS8(x) do { \
    if (x == (int8_t)0x80) { \
        x = 0x7f; \
        SET_QC(); \
    } else if (x < 0) { \
        x = -x; \
    }} while (0)
uint32_t HELPER(neon_qabs_s8)(CPUARMState *env, uint32_t x)
{
    neon_s8 vec;
    NEON_UNPACK(neon_s8, vec, x);
    DO_QABS8(vec.v1);
    DO_QABS8(vec.v2);
    DO_QABS8(vec.v3);
    DO_QABS8(vec.v4);
    NEON_PACK(neon_s8, x, vec);
    return x;
}
#undef DO_QABS8

#define DO_QNEG8(x) do { \
    if (x == (int8_t)0x80) { \
        x = 0x7f; \
        SET_QC(); \
    } else { \
        x = -x; \
    }} while (0)
uint32_t HELPER(neon_qneg_s8)(CPUARMState *env, uint32_t x)
{
    neon_s8 vec;
    NEON_UNPACK(neon_s8, vec, x);
    DO_QNEG8(vec.v1);
    DO_QNEG8(vec.v2);
    DO_QNEG8(vec.v3);
    DO_QNEG8(vec.v4);
    NEON_PACK(neon_s8, x, vec);
    return x;
}
#undef DO_QNEG8

#define DO_QABS16(x) do { \
    if (x == (int16_t)0x8000) { \
        x = 0x7fff; \
        SET_QC(); \
    } else if (x < 0) { \
        x = -x; \
    }} while (0)
uint32_t HELPER(neon_qabs_s16)(CPUARMState *env, uint32_t x)
{
    neon_s16 vec;
    NEON_UNPACK(neon_s16, vec, x);
    DO_QABS16(vec.v1);
    DO_QABS16(vec.v2);
    NEON_PACK(neon_s16, x, vec);
    return x;
}
#undef DO_QABS16

#define DO_QNEG16(x) do { \
    if (x == (int16_t)0x8000) { \
        x = 0x7fff; \
        SET_QC(); \
    } else { \
        x = -x; \
    }} while (0)
uint32_t HELPER(neon_qneg_s16)(CPUARMState *env, uint32_t x)
{
    neon_s16 vec;
    NEON_UNPACK(neon_s16, vec, x);
    DO_QNEG16(vec.v1);
    DO_QNEG16(vec.v2);
    NEON_PACK(neon_s16, x, vec);
    return x;
}
#undef DO_QNEG16

uint32_t HELPER(neon_qabs_s32)(CPUARMState *env, uint32_t x)
{
    if (x == SIGNBIT) {
        SET_QC();
        x = ~SIGNBIT;
    } else if ((int32_t)x < 0) {
        x = -x;
    }
    return x;
}

uint32_t HELPER(neon_qneg_s32)(CPUARMState *env, uint32_t x)
{
    if (x == SIGNBIT) {
        SET_QC();
        x = ~SIGNBIT;
    } else {
        x = -x;
    }
    return x;
}

uint64_t HELPER(neon_qabs_s64)(CPUARMState *env, uint64_t x)
{
    if (x == SIGNBIT64) {
        SET_QC();
        x = ~SIGNBIT64;
    } else if ((int64_t)x < 0) {
        x = -x;
    }
    return x;
}

uint64_t HELPER(neon_qneg_s64)(CPUARMState *env, uint64_t x)
{
    if (x == SIGNBIT64) {
        SET_QC();
        x = ~SIGNBIT64;
    } else {
        x = -x;
    }
    return x;
}

/* NEON Float helpers.  */

/* Floating point comparisons produce an integer result.
 * Note that EQ doesn't signal InvalidOp for QNaNs but GE and GT do.
 * Softfloat routines return 0/1, which we convert to the 0/-1 Neon requires.
 */
uint32_t HELPER(neon_ceq_f32)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float32_eq_quiet(make_float32(a), make_float32(b), fpst);
}

uint32_t HELPER(neon_cge_f32)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float32_le(make_float32(b), make_float32(a), fpst);
}

uint32_t HELPER(neon_cgt_f32)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float32_lt(make_float32(b), make_float32(a), fpst);
}

uint32_t HELPER(neon_acge_f32)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float32 f0 = float32_abs(make_float32(a));
    float32 f1 = float32_abs(make_float32(b));
    return -float32_le(f1, f0, fpst);
}

uint32_t HELPER(neon_acgt_f32)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float32 f0 = float32_abs(make_float32(a));
    float32 f1 = float32_abs(make_float32(b));
    return -float32_lt(f1, f0, fpst);
}

uint64_t HELPER(neon_acge_f64)(uint64_t a, uint64_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float64 f0 = float64_abs(make_float64(a));
    float64 f1 = float64_abs(make_float64(b));
    return -float64_le(f1, f0, fpst);
}

uint64_t HELPER(neon_acgt_f64)(uint64_t a, uint64_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float64 f0 = float64_abs(make_float64(a));
    float64 f1 = float64_abs(make_float64(b));
    return -float64_lt(f1, f0, fpst);
}

#define ELEM(V, N, SIZE) (((V) >> ((N) * (SIZE))) & ((1ull << (SIZE)) - 1))

void HELPER(neon_qunzip8)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd0 = rd[0], zd1 = rd[1];
    uint64_t zm0 = rm[0], zm1 = rm[1];

    uint64_t d0 = ELEM(zd0, 0, 8) | (ELEM(zd0, 2, 8) << 8)
        | (ELEM(zd0, 4, 8) << 16) | (ELEM(zd0, 6, 8) << 24)
        | (ELEM(zd1, 0, 8) << 32) | (ELEM(zd1, 2, 8) << 40)
        | (ELEM(zd1, 4, 8) << 48) | (ELEM(zd1, 6, 8) << 56);
    uint64_t d1 = ELEM(zm0, 0, 8) | (ELEM(zm0, 2, 8) << 8)
        | (ELEM(zm0, 4, 8) << 16) | (ELEM(zm0, 6, 8) << 24)
        | (ELEM(zm1, 0, 8) << 32) | (ELEM(zm1, 2, 8) << 40)
        | (ELEM(zm1, 4, 8) << 48) | (ELEM(zm1, 6, 8) << 56);
    uint64_t m0 = ELEM(zd0, 1, 8) | (ELEM(zd0, 3, 8) << 8)
        | (ELEM(zd0, 5, 8) << 16) | (ELEM(zd0, 7, 8) << 24)
        | (ELEM(zd1, 1, 8) << 32) | (ELEM(zd1, 3, 8) << 40)
        | (ELEM(zd1, 5, 8) << 48) | (ELEM(zd1, 7, 8) << 56);
    uint64_t m1 = ELEM(zm0, 1, 8) | (ELEM(zm0, 3, 8) << 8)
        | (ELEM(zm0, 5, 8) << 16) | (ELEM(zm0, 7, 8) << 24)
        | (ELEM(zm1, 1, 8) << 32) | (ELEM(zm1, 3, 8) << 40)
        | (ELEM(zm1, 5, 8) << 48) | (ELEM(zm1, 7, 8) << 56);

    rm[0] = m0;
    rm[1] = m1;
    rd[0] = d0;
    rd[1] = d1;
}

void HELPER(neon_qunzip16)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd0 = rd[0], zd1 = rd[1];
    uint64_t zm0 = rm[0], zm1 = rm[1];

    uint64_t d0 = ELEM(zd0, 0, 16) | (ELEM(zd0, 2, 16) << 16)
        | (ELEM(zd1, 0, 16) << 32) | (ELEM(zd1, 2, 16) << 48);
    uint64_t d1 = ELEM(zm0, 0, 16) | (ELEM(zm0, 2, 16) << 16)
        | (ELEM(zm1, 0, 16) << 32) | (ELEM(zm1, 2, 16) << 48);
    uint64_t m0 = ELEM(zd0, 1, 16) | (ELEM(zd0, 3, 16) << 16)
        | (ELEM(zd1, 1, 16) << 32) | (ELEM(zd1, 3, 16) << 48);
    uint64_t m1 = ELEM(zm0, 1, 16) | (ELEM(zm0, 3, 16) << 16)
        | (ELEM(zm1, 1, 16) << 32) | (ELEM(zm1, 3, 16) << 48);

    rm[0] = m0;
    rm[1] = m1;
    rd[0] = d0;
    rd[1] = d1;
}

void HELPER(neon_qunzip32)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd0 = rd[0], zd1 = rd[1];
    uint64_t zm0 = rm[0], zm1 = rm[1];

    uint64_t d0 = ELEM(zd0, 0, 32) | (ELEM(zd1, 0, 32) << 32);
    uint64_t d1 = ELEM(zm0, 0, 32) | (ELEM(zm1, 0, 32) << 32);
    uint64_t m0 = ELEM(zd0, 1, 32) | (ELEM(zd1, 1, 32) << 32);
    uint64_t m1 = ELEM(zm0, 1, 32) | (ELEM(zm1, 1, 32) << 32);

    rm[0] = m0;
    rm[1] = m1;
    rd[0] = d0;
    rd[1] = d1;
}

void HELPER(neon_unzip8)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd = rd[0], zm = rm[0];

    uint64_t d0 = ELEM(zd, 0, 8) | (ELEM(zd, 2, 8) << 8)
        | (ELEM(zd, 4, 8) << 16) | (ELEM(zd, 6, 8) << 24)
        | (ELEM(zm, 0, 8) << 32) | (ELEM(zm, 2, 8) << 40)
        | (ELEM(zm, 4, 8) << 48) | (ELEM(zm, 6, 8) << 56);
    uint64_t m0 = ELEM(zd, 1, 8) | (ELEM(zd, 3, 8) << 8)
        | (ELEM(zd, 5, 8) << 16) | (ELEM(zd, 7, 8) << 24)
        | (ELEM(zm, 1, 8) << 32) | (ELEM(zm, 3, 8) << 40)
        | (ELEM(zm, 5, 8) << 48) | (ELEM(zm, 7, 8) << 56);

    rm[0] = m0;
    rd[0] = d0;
}

void HELPER(neon_unzip16)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd = rd[0], zm = rm[0];

    uint64_t d0 = ELEM(zd, 0, 16) | (ELEM(zd, 2, 16) << 16)
        | (ELEM(zm, 0, 16) << 32) | (ELEM(zm, 2, 16) << 48);
    uint64_t m0 = ELEM(zd, 1, 16) | (ELEM(zd, 3, 16) << 16)
        | (ELEM(zm, 1, 16) << 32) | (ELEM(zm, 3, 16) << 48);

    rm[0] = m0;
    rd[0] = d0;
}

void HELPER(neon_qzip8)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd0 = rd[0], zd1 = rd[1];
    uint64_t zm0 = rm[0], zm1 = rm[1];

    uint64_t d0 = ELEM(zd0, 0, 8) | (ELEM(zm0, 0, 8) << 8)
        | (ELEM(zd0, 1, 8) << 16) | (ELEM(zm0, 1, 8) << 24)
        | (ELEM(zd0, 2, 8) << 32) | (ELEM(zm0, 2, 8) << 40)
        | (ELEM(zd0, 3, 8) << 48) | (ELEM(zm0, 3, 8) << 56);
    uint64_t d1 = ELEM(zd0, 4, 8) | (ELEM(zm0, 4, 8) << 8)
        | (ELEM(zd0, 5, 8) << 16) | (ELEM(zm0, 5, 8) << 24)
        | (ELEM(zd0, 6, 8) << 32) | (ELEM(zm0, 6, 8) << 40)
        | (ELEM(zd0, 7, 8) << 48) | (ELEM(zm0, 7, 8) << 56);
    uint64_t m0 = ELEM(zd1, 0, 8) | (ELEM(zm1, 0, 8) << 8)
        | (ELEM(zd1, 1, 8) << 16) | (ELEM(zm1, 1, 8) << 24)
        | (ELEM(zd1, 2, 8) << 32) | (ELEM(zm1, 2, 8) << 40)
        | (ELEM(zd1, 3, 8) << 48) | (ELEM(zm1, 3, 8) << 56);
    uint64_t m1 = ELEM(zd1, 4, 8) | (ELEM(zm1, 4, 8) << 8)
        | (ELEM(zd1, 5, 8) << 16) | (ELEM(zm1, 5, 8) << 24)
        | (ELEM(zd1, 6, 8) << 32) | (ELEM(zm1, 6, 8) << 40)
        | (ELEM(zd1, 7, 8) << 48) | (ELEM(zm1, 7, 8) << 56);

    rm[0] = m0;
    rm[1] = m1;
    rd[0] = d0;
    rd[1] = d1;
}

void HELPER(neon_qzip16)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd0 = rd[0], zd1 = rd[1];
    uint64_t zm0 = rm[0], zm1 = rm[1];

    uint64_t d0 = ELEM(zd0, 0, 16) | (ELEM(zm0, 0, 16) << 16)
        | (ELEM(zd0, 1, 16) << 32) | (ELEM(zm0, 1, 16) << 48);
    uint64_t d1 = ELEM(zd0, 2, 16) | (ELEM(zm0, 2, 16) << 16)
        | (ELEM(zd0, 3, 16) << 32) | (ELEM(zm0, 3, 16) << 48);
    uint64_t m0 = ELEM(zd1, 0, 16) | (ELEM(zm1, 0, 16) << 16)
        | (ELEM(zd1, 1, 16) << 32) | (ELEM(zm1, 1, 16) << 48);
    uint64_t m1 = ELEM(zd1, 2, 16) | (ELEM(zm1, 2, 16) << 16)
        | (ELEM(zd1, 3, 16) << 32) | (ELEM(zm1, 3, 16) << 48);

    rm[0] = m0;
    rm[1] = m1;
    rd[0] = d0;
    rd[1] = d1;
}

void HELPER(neon_qzip32)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd0 = rd[0], zd1 = rd[1];
    uint64_t zm0 = rm[0], zm1 = rm[1];

    uint64_t d0 = ELEM(zd0, 0, 32) | (ELEM(zm0, 0, 32) << 32);
    uint64_t d1 = ELEM(zd0, 1, 32) | (ELEM(zm0, 1, 32) << 32);
    uint64_t m0 = ELEM(zd1, 0, 32) | (ELEM(zm1, 0, 32) << 32);
    uint64_t m1 = ELEM(zd1, 1, 32) | (ELEM(zm1, 1, 32) << 32);

    rm[0] = m0;
    rm[1] = m1;
    rd[0] = d0;
    rd[1] = d1;
}

void HELPER(neon_zip8)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd = rd[0], zm = rm[0];

    uint64_t d0 = ELEM(zd, 0, 8) | (ELEM(zm, 0, 8) << 8)
        | (ELEM(zd, 1, 8) << 16) | (ELEM(zm, 1, 8) << 24)
        | (ELEM(zd, 2, 8) << 32) | (ELEM(zm, 2, 8) << 40)
        | (ELEM(zd, 3, 8) << 48) | (ELEM(zm, 3, 8) << 56);
    uint64_t m0 = ELEM(zd, 4, 8) | (ELEM(zm, 4, 8) << 8)
        | (ELEM(zd, 5, 8) << 16) | (ELEM(zm, 5, 8) << 24)
        | (ELEM(zd, 6, 8) << 32) | (ELEM(zm, 6, 8) << 40)
        | (ELEM(zd, 7, 8) << 48) | (ELEM(zm, 7, 8) << 56);

    rm[0] = m0;
    rd[0] = d0;
}

void HELPER(neon_zip16)(void *vd, void *vm)
{
    uint64_t *rd = vd, *rm = vm;
    uint64_t zd = rd[0], zm = rm[0];

    uint64_t d0 = ELEM(zd, 0, 16) | (ELEM(zm, 0, 16) << 16)
        | (ELEM(zd, 1, 16) << 32) | (ELEM(zm, 1, 16) << 48);
    uint64_t m0 = ELEM(zd, 2, 16) | (ELEM(zm, 2, 16) << 16)
        | (ELEM(zd, 3, 16) << 32) | (ELEM(zm, 3, 16) << 48);

    rm[0] = m0;
    rd[0] = d0;
}
