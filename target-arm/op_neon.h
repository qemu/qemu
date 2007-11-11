/*
 * ARM NEON vector operations.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */
/* Note that for NEON an "l" prefix means it is a wide operation, unlike
   scalar arm ops where it means a word size operation.  */

/* ??? NEON ops should probably have their own float status.  */
#define NFS &env->vfp.fp_status
#define NEON_OP(name) void OPPROTO op_neon_##name (void)

NEON_OP(getreg_T0)
{
    T0 = *(uint32_t *)((char *) env + PARAM1);
}

NEON_OP(getreg_T1)
{
    T1 = *(uint32_t *)((char *) env + PARAM1);
}

NEON_OP(getreg_T2)
{
    T2 = *(uint32_t *)((char *) env + PARAM1);
}

NEON_OP(setreg_T0)
{
    *(uint32_t *)((char *) env + PARAM1) = T0;
}

NEON_OP(setreg_T1)
{
    *(uint32_t *)((char *) env + PARAM1) = T1;
}

NEON_OP(setreg_T2)
{
    *(uint32_t *)((char *) env + PARAM1) = T2;
}

#define NEON_TYPE1(name, type) \
typedef struct \
{ \
    type v1; \
} neon_##name;
#ifdef WORDS_BIGENDIAN
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

#define NEON_VOP(name, vtype, n) \
NEON_OP(name) \
{ \
    vtype vsrc1; \
    vtype vsrc2; \
    vtype vdest; \
    NEON_UNPACK(vtype, vsrc1, T0); \
    NEON_UNPACK(vtype, vsrc2, T1); \
    NEON_DO##n; \
    NEON_PACK(vtype, T0, vdest); \
    FORCE_RET(); \
}

#define NEON_VOP1(name, vtype, n) \
NEON_OP(name) \
{ \
    vtype vsrc1; \
    vtype vdest; \
    NEON_UNPACK(vtype, vsrc1, T0); \
    NEON_DO##n; \
    NEON_PACK(vtype, T0, vdest); \
    FORCE_RET(); \
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
NEON_OP(name) \
{ \
    vtype vsrc1; \
    vtype vsrc2; \
    vtype vdest; \
    NEON_UNPACK(vtype, vsrc1, T0); \
    NEON_UNPACK(vtype, vsrc2, T1); \
    NEON_PDO##n; \
    NEON_PACK(vtype, T0, vdest); \
    FORCE_RET(); \
}

#define NEON_FN(dest, src1, src2) dest = (src1 + src2) >> 1
NEON_VOP(hadd_s8, neon_s8, 4)
NEON_VOP(hadd_u8, neon_u8, 4)
NEON_VOP(hadd_s16, neon_s16, 2)
NEON_VOP(hadd_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(hadd_s32)
{
    int32_t src1 = T0;
    int32_t src2 = T1;
    int32_t dest;

    dest = (src1 >> 1) + (src2 >> 1);
    if (src1 & src2 & 1)
        dest++;
    T0 = dest;
    FORCE_RET();
}

NEON_OP(hadd_u32)
{
    uint32_t src1 = T0;
    uint32_t src2 = T1;
    uint32_t dest;

    dest = (src1 >> 1) + (src2 >> 1);
    if (src1 & src2 & 1)
        dest++;
    T0 = dest;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = (src1 + src2 + 1) >> 1
NEON_VOP(rhadd_s8, neon_s8, 4)
NEON_VOP(rhadd_u8, neon_u8, 4)
NEON_VOP(rhadd_s16, neon_s16, 2)
NEON_VOP(rhadd_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(rhadd_s32)
{
    int32_t src1 = T0;
    int32_t src2 = T1;
    int32_t dest;

    dest = (src1 >> 1) + (src2 >> 1);
    if ((src1 | src2) & 1)
        dest++;
    T0 = dest;
    FORCE_RET();
}

NEON_OP(rhadd_u32)
{
    uint32_t src1 = T0;
    uint32_t src2 = T1;
    uint32_t dest;

    dest = (src1 >> 1) + (src2 >> 1);
    if ((src1 | src2) & 1)
        dest++;
    T0 = dest;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = (src1 - src2) >> 1
NEON_VOP(hsub_s8, neon_s8, 4)
NEON_VOP(hsub_u8, neon_u8, 4)
NEON_VOP(hsub_s16, neon_s16, 2)
NEON_VOP(hsub_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(hsub_s32)
{
    int32_t src1 = T0;
    int32_t src2 = T1;
    int32_t dest;

    dest = (src1 >> 1) - (src2 >> 1);
    if ((~src1) & src2 & 1)
        dest--;
    T0 = dest;
    FORCE_RET();
}

NEON_OP(hsub_u32)
{
    uint32_t src1 = T0;
    uint32_t src2 = T1;
    uint32_t dest;

    dest = (src1 >> 1) - (src2 >> 1);
    if ((~src1) & src2 & 1)
        dest--;
    T0 = dest;
    FORCE_RET();
}

/* ??? bsl, bif and bit are all the same op, just with the oparands in a
   differnet order.  It's currently easier to have 3 differnt ops than
   rearange the operands.  */

/* Bitwise Select.  */
NEON_OP(bsl)
{
    T0 = (T0 & T2) | (T1 & ~T2);
}

/* Bitwise Insert If True.  */
NEON_OP(bit)
{
    T0 = (T0 & T1) | (T2 & ~T1);
}

/* Bitwise Insert If False.  */
NEON_OP(bif)
{
    T0 = (T2 & T1) | (T0 & ~T1);
}

#define NEON_USAT(dest, src1, src2, type) do { \
    uint32_t tmp = (uint32_t)src1 + (uint32_t)src2; \
    if (tmp != (type)tmp) { \
        env->QF = 1; \
        dest = ~0; \
    } else { \
        dest = tmp; \
    }} while(0)
#define NEON_FN(dest, src1, src2) NEON_USAT(dest, src1, src2, uint8_t)
NEON_VOP(qadd_u8, neon_u8, 4)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_USAT(dest, src1, src2, uint16_t)
NEON_VOP(qadd_u16, neon_u16, 2)
#undef NEON_FN
#undef NEON_USAT

#define NEON_SSAT(dest, src1, src2, type) do { \
    int32_t tmp = (uint32_t)src1 + (uint32_t)src2; \
    if (tmp != (type)tmp) { \
        env->QF = 1; \
        if (src2 > 0) { \
            tmp = (1 << (sizeof(type) * 8 - 1)) - 1; \
        } else { \
            tmp = 1 << (sizeof(type) * 8 - 1); \
        } \
    } \
    dest = tmp; \
    } while(0)
#define NEON_FN(dest, src1, src2) NEON_SSAT(dest, src1, src2, int8_t)
NEON_VOP(qadd_s8, neon_s8, 4)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_SSAT(dest, src1, src2, int16_t)
NEON_VOP(qadd_s16, neon_s16, 2)
#undef NEON_FN
#undef NEON_SSAT

#define NEON_USAT(dest, src1, src2, type) do { \
    uint32_t tmp = (uint32_t)src1 - (uint32_t)src2; \
    if (tmp != (type)tmp) { \
        env->QF = 1; \
        dest = 0; \
    } else { \
        dest = tmp; \
    }} while(0)
#define NEON_FN(dest, src1, src2) NEON_USAT(dest, src1, src2, uint8_t)
NEON_VOP(qsub_u8, neon_u8, 4)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_USAT(dest, src1, src2, uint16_t)
NEON_VOP(qsub_u16, neon_u16, 2)
#undef NEON_FN
#undef NEON_USAT

#define NEON_SSAT(dest, src1, src2, type) do { \
    int32_t tmp = (uint32_t)src1 - (uint32_t)src2; \
    if (tmp != (type)tmp) { \
        env->QF = 1; \
        if (src2 < 0) { \
            tmp = (1 << (sizeof(type) * 8 - 1)) - 1; \
        } else { \
            tmp = 1 << (sizeof(type) * 8 - 1); \
        } \
    } \
    dest = tmp; \
    } while(0)
#define NEON_FN(dest, src1, src2) NEON_SSAT(dest, src1, src2, int8_t)
NEON_VOP(qsub_s8, neon_s8, 4)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_SSAT(dest, src1, src2, int16_t)
NEON_VOP(qsub_s16, neon_s16, 2)
#undef NEON_FN
#undef NEON_SSAT

#define NEON_FN(dest, src1, src2) dest = (src1 > src2) ? ~0 : 0
NEON_VOP(cgt_s8, neon_s8, 4)
NEON_VOP(cgt_u8, neon_u8, 4)
NEON_VOP(cgt_s16, neon_s16, 2)
NEON_VOP(cgt_u16, neon_u16, 2)
NEON_VOP(cgt_s32, neon_s32, 1)
NEON_VOP(cgt_u32, neon_u32, 1)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) dest = (src1 >= src2) ? ~0 : 0
NEON_VOP(cge_s8, neon_s8, 4)
NEON_VOP(cge_u8, neon_u8, 4)
NEON_VOP(cge_s16, neon_s16, 2)
NEON_VOP(cge_u16, neon_u16, 2)
NEON_VOP(cge_s32, neon_s32, 1)
NEON_VOP(cge_u32, neon_u32, 1)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) do { \
    int8_t tmp; \
    tmp = (int8_t)src2; \
    if (tmp < 0) { \
        dest = src1 >> -tmp; \
    } else { \
        dest = src1 << tmp; \
    }} while (0)
NEON_VOP(shl_s8, neon_s8, 4)
NEON_VOP(shl_u8, neon_u8, 4)
NEON_VOP(shl_s16, neon_s16, 2)
NEON_VOP(shl_u16, neon_u16, 2)
NEON_VOP(shl_s32, neon_s32, 1)
NEON_VOP(shl_u32, neon_u32, 1)
#undef NEON_FN

NEON_OP(shl_u64)
{
    int8_t shift = T2;
    uint64_t val = T0 | ((uint64_t)T1 << 32);
    if (shift < 0) {
        val >>= -shift;
    } else {
        val <<= shift;
    }
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

NEON_OP(shl_s64)
{
    int8_t shift = T2;
    int64_t val = T0 | ((uint64_t)T1 << 32);
    if (shift < 0) {
        val >>= -shift;
    } else {
        val <<= shift;
    }
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) do { \
    int8_t tmp; \
    tmp = (int8_t)src1; \
    if (tmp < 0) { \
        dest = (src2 + (1 << (-1 - tmp))) >> -tmp; \
    } else { \
        dest = src2 << tmp; \
    }} while (0)

NEON_VOP(rshl_s8, neon_s8, 4)
NEON_VOP(rshl_u8, neon_u8, 4)
NEON_VOP(rshl_s16, neon_s16, 2)
NEON_VOP(rshl_u16, neon_u16, 2)
NEON_VOP(rshl_s32, neon_s32, 1)
NEON_VOP(rshl_u32, neon_u32, 1)
#undef NEON_FN

NEON_OP(rshl_u64)
{
    int8_t shift = T2;
    uint64_t val = T0 | ((uint64_t)T1 << 32);
    if (shift < 0) {
        val = (val + ((uint64_t)1 << (-1 - shift))) >> -shift;
        val >>= -shift;
    } else {
        val <<= shift;
    }
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

NEON_OP(rshl_s64)
{
    int8_t shift = T2;
    int64_t val = T0 | ((uint64_t)T1 << 32);
    if (shift < 0) {
        val = (val + ((int64_t)1 << (-1 - shift))) >> -shift;
    } else {
        val <<= shift;
    }
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) do { \
    int8_t tmp; \
    tmp = (int8_t)src1; \
    if (tmp < 0) { \
        dest = src2 >> -tmp; \
    } else { \
        dest = src2 << tmp; \
        if ((dest >> tmp) != src2) { \
            env->QF = 1; \
            dest = ~0; \
        } \
    }} while (0)
NEON_VOP(qshl_s8, neon_s8, 4)
NEON_VOP(qshl_s16, neon_s16, 2)
NEON_VOP(qshl_s32, neon_s32, 1)
#undef NEON_FN

NEON_OP(qshl_s64)
{
    int8_t shift = T2;
    int64_t val = T0 | ((uint64_t)T1 << 32);
    if (shift < 0) {
        val >>= -shift;
    } else {
        int64_t tmp = val;
        val <<= shift;
        if ((val >> shift) != tmp) {
            env->QF = 1;
            val = (tmp >> 63) ^ 0x7fffffffffffffffULL;
        }
    }
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) do { \
    int8_t tmp; \
    tmp = (int8_t)src1; \
    if (tmp < 0) { \
        dest = src2 >> -tmp; \
    } else { \
        dest = src2 << tmp; \
        if ((dest >> tmp) != src2) { \
            env->QF = 1; \
            dest = src2 >> 31; \
        } \
    }} while (0)
NEON_VOP(qshl_u8, neon_u8, 4)
NEON_VOP(qshl_u16, neon_u16, 2)
NEON_VOP(qshl_u32, neon_u32, 1)
#undef NEON_FN

NEON_OP(qshl_u64)
{
    int8_t shift = T2;
    uint64_t val = T0 | ((uint64_t)T1 << 32);
    if (shift < 0) {
        val >>= -shift;
    } else {
        uint64_t tmp = val;
        val <<= shift;
        if ((val >> shift) != tmp) {
            env->QF = 1;
            val = ~(uint64_t)0;
        }
    }
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) do { \
    int8_t tmp; \
    tmp = (int8_t)src1; \
    if (tmp < 0) { \
        dest = (src2 + (1 << (-1 - tmp))) >> -tmp; \
    } else { \
        dest = src2 << tmp; \
        if ((dest >> tmp) != src2) { \
            dest = ~0; \
        } \
    }} while (0)
NEON_VOP(qrshl_s8, neon_s8, 4)
NEON_VOP(qrshl_s16, neon_s16, 2)
NEON_VOP(qrshl_s32, neon_s32, 1)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) do { \
    int8_t tmp; \
    tmp = (int8_t)src1; \
    if (tmp < 0) { \
        dest = (src2 + (1 << (-1 - tmp))) >> -tmp; \
    } else { \
        dest = src2 << tmp; \
        if ((dest >> tmp) != src2) { \
            env->QF = 1; \
            dest = src2 >> 31; \
        } \
    }} while (0)
NEON_VOP(qrshl_u8, neon_u8, 4)
NEON_VOP(qrshl_u16, neon_u16, 2)
NEON_VOP(qrshl_u32, neon_u32, 1)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) dest = (src1 > src2) ? src1 : src2
NEON_VOP(max_s8, neon_s8, 4)
NEON_VOP(max_u8, neon_u8, 4)
NEON_VOP(max_s16, neon_s16, 2)
NEON_VOP(max_u16, neon_u16, 2)
NEON_VOP(max_s32, neon_s32, 1)
NEON_VOP(max_u32, neon_u32, 1)
NEON_POP(pmax_s8, neon_s8, 4)
NEON_POP(pmax_u8, neon_u8, 4)
NEON_POP(pmax_s16, neon_s16, 2)
NEON_POP(pmax_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(max_f32)
{
    float32 f0 = vfp_itos(T0);
    float32 f1 = vfp_itos(T1);
    T0 = (float32_compare_quiet(f0, f1, NFS) == 1) ? T0 : T1;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = (src1 < src2) ? src1 : src2
NEON_VOP(min_s8, neon_s8, 4)
NEON_VOP(min_u8, neon_u8, 4)
NEON_VOP(min_s16, neon_s16, 2)
NEON_VOP(min_u16, neon_u16, 2)
NEON_VOP(min_s32, neon_s32, 1)
NEON_VOP(min_u32, neon_u32, 1)
NEON_POP(pmin_s8, neon_s8, 4)
NEON_POP(pmin_u8, neon_u8, 4)
NEON_POP(pmin_s16, neon_s16, 2)
NEON_POP(pmin_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(min_f32)
{
    float32 f0 = vfp_itos(T0);
    float32 f1 = vfp_itos(T1);
    T0 = (float32_compare_quiet(f0, f1, NFS) == -1) ? T0 : T1;
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) \
    dest = (src1 > src2) ? (src1 - src2) : (src2 - src1)
NEON_VOP(abd_s8, neon_s8, 4)
NEON_VOP(abd_u8, neon_u8, 4)
NEON_VOP(abd_s16, neon_s16, 2)
NEON_VOP(abd_u16, neon_u16, 2)
NEON_VOP(abd_s32, neon_s32, 1)
NEON_VOP(abd_u32, neon_u32, 1)
#undef NEON_FN

NEON_OP(abd_f32)
{
    float32 f0 = vfp_itos(T0);
    float32 f1 = vfp_itos(T1);
    T0 = vfp_stoi((float32_compare_quiet(f0, f1, NFS) == 1)
                  ? float32_sub(f0, f1, NFS)
                  : float32_sub(f1, f0, NFS));
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = src1 + src2
NEON_VOP(add_u8, neon_u8, 4)
NEON_VOP(add_u16, neon_u16, 2)
NEON_POP(padd_u8, neon_u8, 4)
NEON_POP(padd_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(add_f32)
{
    T0 = vfp_stoi(float32_add(vfp_itos(T0), vfp_itos(T1), NFS));
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = src1 - src2
NEON_VOP(sub_u8, neon_u8, 4)
NEON_VOP(sub_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(sub_f32)
{
    T0 = vfp_stoi(float32_sub(vfp_itos(T0), vfp_itos(T1), NFS));
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = src2 - src1
NEON_VOP(rsb_u8, neon_u8, 4)
NEON_VOP(rsb_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(rsb_f32)
{
    T0 = vfp_stoi(float32_sub(vfp_itos(T1), vfp_itos(T0), NFS));
    FORCE_RET();
}

#define NEON_FN(dest, src1, src2) dest = src1 * src2
NEON_VOP(mul_u8, neon_u8, 4)
NEON_VOP(mul_u16, neon_u16, 2)
#undef NEON_FN

NEON_OP(mul_f32)
{
    T0 = vfp_stoi(float32_mul(vfp_itos(T0), vfp_itos(T1), NFS));
    FORCE_RET();
}

NEON_OP(mul_p8)
{
    T0 = helper_neon_mul_p8(T0, T1);
}

#define NEON_FN(dest, src1, src2) dest = (src1 & src2) ? -1 : 0
NEON_VOP(tst_u8, neon_u8, 4)
NEON_VOP(tst_u16, neon_u16, 2)
NEON_VOP(tst_u32, neon_u32, 1)
#undef NEON_FN

#define NEON_FN(dest, src1, src2) dest = (src1 == src2) ? -1 : 0
NEON_VOP(ceq_u8, neon_u8, 4)
NEON_VOP(ceq_u16, neon_u16, 2)
NEON_VOP(ceq_u32, neon_u32, 1)
#undef NEON_FN

#define NEON_QDMULH16(dest, src1, src2, round) do { \
    uint32_t tmp = (int32_t)(int16_t) src1 * (int16_t) src2; \
    if ((tmp ^ (tmp << 1)) & SIGNBIT) { \
        env->QF = 1; \
        tmp = (tmp >> 31) ^ ~SIGNBIT; \
    } \
    tmp <<= 1; \
    if (round) { \
        int32_t old = tmp; \
        tmp += 1 << 15; \
        if ((int32_t)tmp < old) { \
            env->QF = 1; \
            tmp = SIGNBIT - 1; \
        } \
    } \
    dest = tmp >> 16; \
    } while(0)
#define NEON_FN(dest, src1, src2) NEON_QDMULH16(dest, src1, src2, 0)
NEON_VOP(qdmulh_s16, neon_s16, 2)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_QDMULH16(dest, src1, src2, 1)
NEON_VOP(qrdmulh_s16, neon_s16, 2)
#undef NEON_FN
#undef NEON_QDMULH16

#define SIGNBIT64 ((uint64_t)1 << 63)
#define NEON_QDMULH32(dest, src1, src2, round) do { \
    uint64_t tmp = (int64_t)(int32_t) src1 * (int32_t) src2; \
    if ((tmp ^ (tmp << 1)) & SIGNBIT64) { \
        env->QF = 1; \
        tmp = (tmp >> 63) ^ ~SIGNBIT64; \
    } else { \
        tmp <<= 1; \
    } \
    if (round) { \
        int64_t old = tmp; \
        tmp += (int64_t)1 << 31; \
        if ((int64_t)tmp < old) { \
            env->QF = 1; \
            tmp = SIGNBIT64 - 1; \
        } \
    } \
    dest = tmp >> 32; \
    } while(0)
#define NEON_FN(dest, src1, src2) NEON_QDMULH32(dest, src1, src2, 0)
NEON_VOP(qdmulh_s32, neon_s32, 1)
#undef NEON_FN
#define NEON_FN(dest, src1, src2) NEON_QDMULH32(dest, src1, src2, 1)
NEON_VOP(qrdmulh_s32, neon_s32, 1)
#undef NEON_FN
#undef NEON_QDMULH32

NEON_OP(recps_f32)
{
    T0 = vfp_stoi(helper_recps_f32(vfp_itos(T0), vfp_itos(T1)));
    FORCE_RET();
}

NEON_OP(rsqrts_f32)
{
    T0 = vfp_stoi(helper_rsqrts_f32(vfp_itos(T0), vfp_itos(T1)));
    FORCE_RET();
}

/* Floating point comparisons produce an integer result.  */
#define NEON_VOP_FCMP(name, cmp) \
NEON_OP(name) \
{ \
    if (float32_compare_quiet(vfp_itos(T0), vfp_itos(T1), NFS) cmp 0) \
        T0 = -1; \
    else \
        T0 = 0; \
    FORCE_RET(); \
}

NEON_VOP_FCMP(ceq_f32, ==)
NEON_VOP_FCMP(cge_f32, >=)
NEON_VOP_FCMP(cgt_f32, >)

NEON_OP(acge_f32)
{
    float32 f0 = float32_abs(vfp_itos(T0));
    float32 f1 = float32_abs(vfp_itos(T1));
    T0 = (float32_compare_quiet(f0, f1,NFS) >= 0) ? -1 : 0;
    FORCE_RET();
}

NEON_OP(acgt_f32)
{
    float32 f0 = float32_abs(vfp_itos(T0));
    float32 f1 = float32_abs(vfp_itos(T1));
    T0 = (float32_compare_quiet(f0, f1, NFS) > 0) ? -1 : 0;
    FORCE_RET();
}

/* Narrowing instructions.  The named type is the destination type.  */
NEON_OP(narrow_u8)
{
    T0 = (T0 & 0xff) | ((T0 >> 8) & 0xff00)
         | ((T1 << 16) & 0xff0000) | (T1 << 24);
    FORCE_RET();
}

NEON_OP(narrow_sat_u8)
{
    neon_u16 src;
    neon_u8 dest;
#define SAT8(d, s) \
    if (s > 0xff) { \
        d = 0xff; \
        env->QF = 1; \
    } else  { \
        d = s; \
    }

    NEON_UNPACK(neon_u16, src, T0);
    SAT8(dest.v1, src.v1);
    SAT8(dest.v2, src.v2);
    NEON_UNPACK(neon_u16, src, T1);
    SAT8(dest.v3, src.v1);
    SAT8(dest.v4, src.v2);
    NEON_PACK(neon_u8, T0, dest);
    FORCE_RET();
#undef SAT8
}

NEON_OP(narrow_sat_s8)
{
    neon_s16 src;
    neon_s8 dest;
#define SAT8(d, s) \
    if (s != (uint8_t)s) { \
        d = (s >> 15) ^ 0x7f; \
        env->QF = 1; \
    } else  { \
        d = s; \
    }

    NEON_UNPACK(neon_s16, src, T0);
    SAT8(dest.v1, src.v1);
    SAT8(dest.v2, src.v2);
    NEON_UNPACK(neon_s16, src, T1);
    SAT8(dest.v3, src.v1);
    SAT8(dest.v4, src.v2);
    NEON_PACK(neon_s8, T0, dest);
    FORCE_RET();
#undef SAT8
}

NEON_OP(narrow_u16)
{
    T0 = (T0 & 0xffff) | (T1 << 16);
}

NEON_OP(narrow_sat_u16)
{
    if (T0 > 0xffff) {
        T0 = 0xffff;
        env->QF = 1;
    }
    if (T1 > 0xffff) {
        T1 = 0xffff;
        env->QF = 1;
    }
    T0 |= T1 << 16;
    FORCE_RET();
}

NEON_OP(narrow_sat_s16)
{
    if ((int32_t)T0 != (int16_t)T0) {
        T0 = ((int32_t)T0 >> 31) ^ 0x7fff;
        env->QF = 1;
    }
    if ((int32_t)T1 != (int16_t) T1) {
        T1 = ((int32_t)T1 >> 31) ^ 0x7fff;
        env->QF = 1;
    }
    T0 = (uint16_t)T0 | (T1 << 16);
    FORCE_RET();
}

NEON_OP(narrow_sat_u32)
{
    if (T1) {
        T0 = 0xffffffffu;
        env->QF = 1;
    }
    FORCE_RET();
}

NEON_OP(narrow_sat_s32)
{
    int32_t sign = (int32_t)T1 >> 31;

    if ((int32_t)T1 != sign) {
        T0 = sign ^ 0x7fffffff;
        env->QF = 1;
    }
    FORCE_RET();
}

/* Narrowing instructions.  Named type is the narrow type.  */
NEON_OP(narrow_high_u8)
{
    T0 = ((T0 >> 8) & 0xff) | ((T0 >> 16) & 0xff00)
        | ((T1 << 8) & 0xff0000) | (T1 & 0xff000000);
    FORCE_RET();
}

NEON_OP(narrow_high_u16)
{
    T0 = (T0 >> 16) | (T1 & 0xffff0000);
    FORCE_RET();
}

NEON_OP(narrow_high_round_u8)
{
    T0 = (((T0 + 0x80) >> 8) & 0xff) | (((T0 + 0x800000) >> 16) & 0xff00)
        | (((T1 + 0x80) << 8) & 0xff0000) | ((T1 + 0x800000) & 0xff000000);
    FORCE_RET();
}

NEON_OP(narrow_high_round_u16)
{
    T0 = ((T0 + 0x8000) >> 16) | ((T1 + 0x8000) & 0xffff0000);
    FORCE_RET();
}

NEON_OP(narrow_high_round_u32)
{
    if (T0 >= 0x80000000u)
        T0 = T1 + 1;
    else
        T0 = T1;
    FORCE_RET();
}

/* Widening instructions.  Named type is source type.  */
NEON_OP(widen_s8)
{
    uint32_t src;

    src = T0;
    T0 = (uint16_t)(int8_t)src | ((int8_t)(src >> 8) << 16);
    T1 = (uint16_t)(int8_t)(src >> 16) | ((int8_t)(src >> 24) << 16);
}

NEON_OP(widen_u8)
{
    T1 = ((T0 >> 8) & 0xff0000) | ((T0 >> 16) & 0xff);
    T0 = ((T0 << 8) & 0xff0000) | (T0 & 0xff);
}

NEON_OP(widen_s16)
{
    int32_t src;

    src = T0;
    T0 = (int16_t)src;
    T1 = src >> 16;
}

NEON_OP(widen_u16)
{
    T1 = T0 >> 16;
    T0 &= 0xffff;
}

NEON_OP(widen_s32)
{
    T1 = (int32_t)T0 >> 31;
    FORCE_RET();
}

NEON_OP(widen_high_u8)
{
    T1 = (T0 & 0xff000000) | ((T0 >> 8) & 0xff00);
    T0 = ((T0 << 16) & 0xff000000) | ((T0 << 8) & 0xff00);
}

NEON_OP(widen_high_u16)
{
    T1 = T0 & 0xffff0000;
    T0 <<= 16;
}

/* Long operations.  The type is the wide type.  */
NEON_OP(shll_u16)
{
    int shift = PARAM1;
    uint32_t mask;

    mask = 0xffff >> (16 - shift);
    mask |= mask << 16;
    mask = ~mask;

    T0 = (T0 << shift) & mask;
    T1 = (T1 << shift) & mask;
    FORCE_RET();
}

NEON_OP(shll_u64)
{
    int shift = PARAM1;

    T1 <<= shift;
    T1 |= T0 >> (32 - shift);
    T0 <<= shift;
    FORCE_RET();
}

NEON_OP(addl_u16)
{
    uint32_t tmp;
    uint32_t high;

    tmp = env->vfp.scratch[0];
    high = (T0 >> 16) + (tmp >> 16);
    T0 = (uint16_t)(T0 + tmp);
    T0 |= (high << 16);
    tmp = env->vfp.scratch[1];
    high = (T1 >> 16) + (tmp >> 16);
    T1 = (uint16_t)(T1 + tmp);
    T1 |= (high << 16);
    FORCE_RET();
}

NEON_OP(addl_u32)
{
    T0 += env->vfp.scratch[0];
    T1 += env->vfp.scratch[1];
    FORCE_RET();
}

NEON_OP(addl_u64)
{
    uint64_t tmp;
    tmp = T0 | ((uint64_t)T1 << 32);
    tmp += env->vfp.scratch[0];
    tmp += (uint64_t)env->vfp.scratch[1] << 32;
    T0 = tmp;
    T1 = tmp >> 32;
    FORCE_RET();
}

NEON_OP(subl_u16)
{
    uint32_t tmp;
    uint32_t high;

    tmp = env->vfp.scratch[0];
    high = (T0 >> 16) - (tmp >> 16);
    T0 = (uint16_t)(T0 - tmp);
    T0 |= (high << 16);
    tmp = env->vfp.scratch[1];
    high = (T1 >> 16) - (tmp >> 16);
    T1 = (uint16_t)(T1 - tmp);
    T1 |= (high << 16);
    FORCE_RET();
}

NEON_OP(subl_u32)
{
    T0 -= env->vfp.scratch[0];
    T1 -= env->vfp.scratch[1];
    FORCE_RET();
}

NEON_OP(subl_u64)
{
    uint64_t tmp;
    tmp = T0 | ((uint64_t)T1 << 32);
    tmp -= env->vfp.scratch[0];
    tmp -= (uint64_t)env->vfp.scratch[1] << 32;
    T0 = tmp;
    T1 = tmp >> 32;
    FORCE_RET();
}

#define DO_ABD(dest, x, y, type) do { \
    type tmp_x = x; \
    type tmp_y = y; \
    dest = ((tmp_x > tmp_y) ? tmp_x - tmp_y : tmp_y - tmp_x); \
    } while(0)

NEON_OP(abdl_u16)
{
    uint32_t tmp;
    uint32_t low;
    uint32_t high;

    DO_ABD(low, T0, T1, uint8_t);
    DO_ABD(tmp, T0 >> 8, T1 >> 8, uint8_t);
    low |= tmp << 16;
    DO_ABD(high, T0 >> 16, T1 >> 16, uint8_t);
    DO_ABD(tmp, T0 >> 24, T1 >> 24, uint8_t);
    high |= tmp << 16;
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(abdl_s16)
{
    uint32_t tmp;
    uint32_t low;
    uint32_t high;

    DO_ABD(low, T0, T1, int8_t);
    DO_ABD(tmp, T0 >> 8, T1 >> 8, int8_t);
    low |= tmp << 16;
    DO_ABD(high, T0 >> 16, T1 >> 16, int8_t);
    DO_ABD(tmp, T0 >> 24, T1 >> 24, int8_t);
    high |= tmp << 16;
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(abdl_u32)
{
    uint32_t low;
    uint32_t high;

    DO_ABD(low, T0, T1, uint16_t);
    DO_ABD(high, T0 >> 16, T1 >> 16, uint16_t);
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(abdl_s32)
{
    uint32_t low;
    uint32_t high;

    DO_ABD(low, T0, T1, int16_t);
    DO_ABD(high, T0 >> 16, T1 >> 16, int16_t);
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(abdl_u64)
{
    DO_ABD(T0, T0, T1, uint32_t);
    T1 = 0;
}

NEON_OP(abdl_s64)
{
    DO_ABD(T0, T0, T1, int32_t);
    T1 = 0;
}
#undef DO_ABD

/* Widening multiple. Named type is the source type.  */
#define DO_MULL(dest, x, y, type1, type2) do { \
    type1 tmp_x = x; \
    type1 tmp_y = y; \
    dest = (type2)((type2)tmp_x * (type2)tmp_y); \
    } while(0)

NEON_OP(mull_u8)
{
    uint32_t tmp;
    uint32_t low;
    uint32_t high;

    DO_MULL(low, T0, T1, uint8_t, uint16_t);
    DO_MULL(tmp, T0 >> 8, T1 >> 8, uint8_t, uint16_t);
    low |= tmp << 16;
    DO_MULL(high, T0 >> 16, T1 >> 16, uint8_t, uint16_t);
    DO_MULL(tmp, T0 >> 24, T1 >> 24, uint8_t, uint16_t);
    high |= tmp << 16;
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(mull_s8)
{
    uint32_t tmp;
    uint32_t low;
    uint32_t high;

    DO_MULL(low, T0, T1, int8_t, uint16_t);
    DO_MULL(tmp, T0 >> 8, T1 >> 8, int8_t, uint16_t);
    low |= tmp << 16;
    DO_MULL(high, T0 >> 16, T1 >> 16, int8_t, uint16_t);
    DO_MULL(tmp, T0 >> 24, T1 >> 24, int8_t, uint16_t);
    high |= tmp << 16;
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(mull_u16)
{
    uint32_t low;
    uint32_t high;

    DO_MULL(low, T0, T1, uint16_t, uint32_t);
    DO_MULL(high, T0 >> 16, T1 >> 16, uint16_t, uint32_t);
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(mull_s16)
{
    uint32_t low;
    uint32_t high;

    DO_MULL(low, T0, T1, int16_t, uint32_t);
    DO_MULL(high, T0 >> 16, T1 >> 16, int16_t, uint32_t);
    T0 = low;
    T1 = high;
    FORCE_RET();
}

NEON_OP(addl_saturate_s32)
{
    uint32_t tmp;
    uint32_t res;

    tmp = env->vfp.scratch[0];
    res = T0 + tmp;
    if (((res ^ T0) & SIGNBIT) && !((T0 ^ tmp) & SIGNBIT)) {
        env->QF = 1;
        T0 = (T0 >> 31) ^ 0x7fffffff;
    } else {
      T0 = res;
    }
    tmp = env->vfp.scratch[1];
    res = T1 + tmp;
    if (((res ^ T1) & SIGNBIT) && !((T1 ^ tmp) & SIGNBIT)) {
        env->QF = 1;
        T1 = (T1 >> 31) ^ 0x7fffffff;
    } else {
      T1 = res;
    }
    FORCE_RET();
}

NEON_OP(addl_saturate_s64)
{
    uint64_t src1;
    uint64_t src2;
    uint64_t res;

    src1 = T0 + ((uint64_t)T1 << 32);
    src2 = env->vfp.scratch[0] + ((uint64_t)env->vfp.scratch[1] << 32);
    res = src1 + src2;
    if (((res ^ src1) & SIGNBIT64) && !((src1 ^ src2) & SIGNBIT64)) {
        env->QF = 1;
        T0 = ~(int64_t)src1 >> 63;
        T1 = T0 ^ 0x80000000;
    } else {
      T0 = res;
      T1 = res >> 32;
    }
    FORCE_RET();
}

NEON_OP(addl_saturate_u64)
{
    uint64_t src1;
    uint64_t src2;
    uint64_t res;

    src1 = T0 + ((uint64_t)T1 << 32);
    src2 = env->vfp.scratch[0] + ((uint64_t)env->vfp.scratch[1] << 32);
    res = src1 + src2;
    if (res < src1) {
        env->QF = 1;
        T0 = 0xffffffff;
        T1 = 0xffffffff;
    } else {
      T0 = res;
      T1 = res >> 32;
    }
    FORCE_RET();
}

NEON_OP(subl_saturate_s64)
{
    uint64_t src1;
    uint64_t src2;
    uint64_t res;

    src1 = T0 + ((uint64_t)T1 << 32);
    src2 = env->vfp.scratch[0] + ((uint64_t)env->vfp.scratch[1] << 32);
    res = src1 - src2;
    if (((res ^ src1) & SIGNBIT64) && ((src1 ^ src2) & SIGNBIT64)) {
        env->QF = 1;
        T0 = ~(int64_t)src1 >> 63;
        T1 = T0 ^ 0x80000000;
    } else {
      T0 = res;
      T1 = res >> 32;
    }
    FORCE_RET();
}

NEON_OP(subl_saturate_u64)
{
    uint64_t src1;
    uint64_t src2;
    uint64_t res;

    src1 = T0 + ((uint64_t)T1 << 32);
    src2 = env->vfp.scratch[0] + ((uint64_t)env->vfp.scratch[1] << 32);
    if (src1 < src2) {
        env->QF = 1;
        T0 = 0;
        T1 = 0;
    } else {
      res = src1 - src2;
      T0 = res;
      T1 = res >> 32;
    }
    FORCE_RET();
}

NEON_OP(negl_u16)
{
    uint32_t tmp;
    tmp = T0 >> 16;
    tmp = -tmp;
    T0 = (-T0 & 0xffff) | (tmp << 16);
    tmp = T1 >> 16;
    tmp = -tmp;
    T1 = (-T1 & 0xffff) | (tmp << 16);
    FORCE_RET();
}

NEON_OP(negl_u32)
{
    T0 = -T0;
    T1 = -T1;
    FORCE_RET();
}

NEON_OP(negl_u64)
{
    uint64_t val;

    val = T0 | ((uint64_t)T1 << 32);
    val = -val;
    T0 = val;
    T1 = val >> 32;
    FORCE_RET();
}

/* Scalar operations.  */
NEON_OP(dup_low16)
{
    T0 = (T0 & 0xffff) | (T0 << 16);
    FORCE_RET();
}

NEON_OP(dup_high16)
{
    T0 = (T0 >> 16) | (T0 & 0xffff0000);
    FORCE_RET();
}

/* Helper for VEXT */
NEON_OP(extract)
{
    int shift = PARAM1;
    T0 = (T0 >> shift) | (T1 << (32 - shift));
    FORCE_RET();
}

/* Pairwise add long.  Named type is source type.  */
NEON_OP(paddl_s8)
{
    int8_t src1;
    int8_t src2;
    uint16_t result;
    src1 = T0 >> 24;
    src2 = T0 >> 16;
    result = (uint16_t)src1 + src2;
    src1 = T0 >> 8;
    src2 = T0;
    T0 = (uint16_t)((uint16_t)src1 + src2) | ((uint32_t)result << 16);
    FORCE_RET();
}

NEON_OP(paddl_u8)
{
    uint8_t src1;
    uint8_t src2;
    uint16_t result;
    src1 = T0 >> 24;
    src2 = T0 >> 16;
    result = (uint16_t)src1 + src2;
    src1 = T0 >> 8;
    src2 = T0;
    T0 = (uint16_t)((uint16_t)src1 + src2) | ((uint32_t)result << 16);
    FORCE_RET();
}

NEON_OP(paddl_s16)
{
    T0 = (uint32_t)(int16_t)T0 + (uint32_t)(int16_t)(T0 >> 16);
    FORCE_RET();
}

NEON_OP(paddl_u16)
{
    T0 = (uint32_t)(uint16_t)T0 + (uint32_t)(uint16_t)(T0 >> 16);
    FORCE_RET();
}

NEON_OP(paddl_s32)
{
    int64_t tmp;
    tmp = (int64_t)(int32_t)T0 + (int64_t)(int32_t)T1;
    T0 = tmp;
    T1 = tmp >> 32;
    FORCE_RET();
}

NEON_OP(paddl_u32)
{
    uint64_t tmp;
    tmp = (uint64_t)T0 + (uint64_t)T1;
    T0 = tmp;
    T1 = tmp >> 32;
    FORCE_RET();
}

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

NEON_OP(clz_u8)
{
    uint32_t result;
    uint32_t tmp;

    tmp = T0;
    result = do_clz8(tmp);
    result |= do_clz8(tmp >> 8) << 8;
    result |= do_clz8(tmp >> 16) << 16;
    result |= do_clz8(tmp >> 24) << 24;
    T0 = result;
    FORCE_RET();
}

NEON_OP(clz_u16)
{
    uint32_t result;
    uint32_t tmp;
    tmp = T0;
    result = do_clz16(tmp);
    result |= do_clz16(tmp >> 16) << 16;
    T0 = result;
    FORCE_RET();
}

NEON_OP(cls_s8)
{
    uint32_t result;
    int8_t tmp;
    tmp = T0;
    result = do_clz8((tmp < 0) ? ~tmp : tmp) - 1;
    tmp = T0 >> 8;
    result |= (do_clz8((tmp < 0) ? ~tmp : tmp) - 1) << 8;
    tmp = T0 >> 16;
    result |= (do_clz8((tmp < 0) ? ~tmp : tmp) - 1) << 16;
    tmp = T0 >> 24;
    result |= (do_clz8((tmp < 0) ? ~tmp : tmp) - 1) << 24;
    T0 = result;
    FORCE_RET();
}

NEON_OP(cls_s16)
{
    uint32_t result;
    int16_t tmp;
    tmp = T0;
    result = do_clz16((tmp < 0) ? ~tmp : tmp) - 1;
    tmp = T0 >> 16;
    result |= (do_clz16((tmp < 0) ? ~tmp : tmp) - 1) << 16;
    T0 = result;
    FORCE_RET();
}

NEON_OP(cls_s32)
{
    int count;
    if ((int32_t)T0 < 0)
        T0 = ~T0;
    for (count = 32; T0 > 0; count--)
        T0 = T0 >> 1;
    T0 = count - 1;
    FORCE_RET();
}

/* Bit count.  */
NEON_OP(cnt_u8)
{
    T0 = (T0 & 0x55555555) + ((T0 >>  1) & 0x55555555);
    T0 = (T0 & 0x33333333) + ((T0 >>  2) & 0x33333333);
    T0 = (T0 & 0x0f0f0f0f) + ((T0 >>  4) & 0x0f0f0f0f);
    FORCE_RET();
}

/* Saturnating negation.  */
/* ??? Make these use NEON_VOP1 */
#define DO_QABS8(x) do { \
    if (x == (int8_t)0x80) { \
        x = 0x7f; \
        env->QF = 1; \
    } else if (x < 0) { \
        x = -x; \
    }} while (0)
NEON_OP(qabs_s8)
{
    neon_s8 vec;
    NEON_UNPACK(neon_s8, vec, T0);
    DO_QABS8(vec.v1);
    DO_QABS8(vec.v2);
    DO_QABS8(vec.v3);
    DO_QABS8(vec.v4);
    NEON_PACK(neon_s8, T0, vec);
    FORCE_RET();
}
#undef DO_QABS8

#define DO_QNEG8(x) do { \
    if (x == (int8_t)0x80) { \
        x = 0x7f; \
        env->QF = 1; \
    } else { \
        x = -x; \
    }} while (0)
NEON_OP(qneg_s8)
{
    neon_s8 vec;
    NEON_UNPACK(neon_s8, vec, T0);
    DO_QNEG8(vec.v1);
    DO_QNEG8(vec.v2);
    DO_QNEG8(vec.v3);
    DO_QNEG8(vec.v4);
    NEON_PACK(neon_s8, T0, vec);
    FORCE_RET();
}
#undef DO_QNEG8

#define DO_QABS16(x) do { \
    if (x == (int16_t)0x8000) { \
        x = 0x7fff; \
        env->QF = 1; \
    } else if (x < 0) { \
        x = -x; \
    }} while (0)
NEON_OP(qabs_s16)
{
    neon_s16 vec;
    NEON_UNPACK(neon_s16, vec, T0);
    DO_QABS16(vec.v1);
    DO_QABS16(vec.v2);
    NEON_PACK(neon_s16, T0, vec);
    FORCE_RET();
}
#undef DO_QABS16

#define DO_QNEG16(x) do { \
    if (x == (int16_t)0x8000) { \
        x = 0x7fff; \
        env->QF = 1; \
    } else { \
        x = -x; \
    }} while (0)
NEON_OP(qneg_s16)
{
    neon_s16 vec;
    NEON_UNPACK(neon_s16, vec, T0);
    DO_QNEG16(vec.v1);
    DO_QNEG16(vec.v2);
    NEON_PACK(neon_s16, T0, vec);
    FORCE_RET();
}
#undef DO_QNEG16

NEON_OP(qabs_s32)
{
    if (T0 == 0x80000000) {
        T0 = 0x7fffffff;
        env->QF = 1;
    } else if ((int32_t)T0 < 0) {
        T0 = -T0;
    }
    FORCE_RET();
}

NEON_OP(qneg_s32)
{
    if (T0 == 0x80000000) {
        T0 = 0x7fffffff;
        env->QF = 1;
    } else {
        T0 = -T0;
    }
    FORCE_RET();
}

/* Unary opperations */
#define NEON_FN(dest, src, dummy) dest = (src < 0) ? -src : src
NEON_VOP1(abs_s8, neon_s8, 4)
NEON_VOP1(abs_s16, neon_s16, 2)
NEON_OP(abs_s32)
{
    if ((int32_t)T0 < 0)
        T0 = -T0;
    FORCE_RET();
}
#undef NEON_FN

/* Transpose.  Argument order is rather strange to avoid special casing
   the tranlation code.
   On input T0 = rm, T1 = rd.  On output T0 = rd, T1 = rm  */
NEON_OP(trn_u8)
{
    uint32_t rd;
    uint32_t rm;
    rd = ((T0 & 0x00ff00ff) << 8) | (T1 & 0x00ff00ff);
    rm = ((T1 & 0xff00ff00) >> 8) | (T0 & 0xff00ff00);
    T0 = rd;
    T1 = rm;
    FORCE_RET();
}

NEON_OP(trn_u16)
{
    uint32_t rd;
    uint32_t rm;
    rd = (T0 << 16) | (T1 & 0xffff);
    rm = (T1 >> 16) | (T0 & 0xffff0000);
    T0 = rd;
    T1 = rm;
    FORCE_RET();
}

/* Worker routines for zip and unzip.  */
NEON_OP(unzip_u8)
{
    uint32_t rd;
    uint32_t rm;
    rd = (T0 & 0xff) | ((T0 >> 8) & 0xff00)
         | ((T1 << 16) & 0xff0000) | ((T1 << 8) & 0xff000000);
    rm = ((T0 >> 8) & 0xff) | ((T0 >> 16) & 0xff00)
         | ((T1 << 8) & 0xff0000) | (T1 & 0xff000000);
    T0 = rd;
    T1 = rm;
    FORCE_RET();
}

NEON_OP(zip_u8)
{
    uint32_t rd;
    uint32_t rm;
    rd = (T0 & 0xff) | ((T1 << 8) & 0xff00)
         | ((T0 << 16) & 0xff0000) | ((T1 << 24) & 0xff000000);
    rm = ((T0 >> 16) & 0xff) | ((T1 >> 8) & 0xff00)
         | ((T0 >> 8) & 0xff0000) | (T1 & 0xff000000);
    T0 = rd;
    T1 = rm;
    FORCE_RET();
}

NEON_OP(zip_u16)
{
    uint32_t tmp;

    tmp = (T0 & 0xffff) | (T1 << 16);
    T1 = (T1 & 0xffff0000) | (T0 >> 16);
    T0 = tmp;
    FORCE_RET();
}

/* Reciprocal/root estimate.  */
NEON_OP(recpe_u32)
{
    T0 = helper_recpe_u32(T0);
}

NEON_OP(rsqrte_u32)
{
    T0 = helper_rsqrte_u32(T0);
}

NEON_OP(recpe_f32)
{
    FT0s = helper_recpe_f32(FT0s);
}

NEON_OP(rsqrte_f32)
{
    FT0s = helper_rsqrte_f32(FT0s);
}

/* Table lookup.  This accessed the register file directly.  */
NEON_OP(tbl)
{
    helper_neon_tbl(PARAM1, PARAM2);
}

NEON_OP(dup_u8)
{
    T0 = (T0 >> PARAM1) & 0xff;
    T0 |= T0 << 8;
    T0 |= T0 << 16;
    FORCE_RET();
}

/* Helpers for element load/store.  */
NEON_OP(insert_elt)
{
    int shift = PARAM1;
    uint32_t mask = PARAM2;
    T2 = (T2 & mask) | (T0 << shift);
    FORCE_RET();
}

NEON_OP(extract_elt)
{
    int shift = PARAM1;
    uint32_t mask = PARAM2;
    T0 = (T2 & mask) >> shift;
    FORCE_RET();
}
