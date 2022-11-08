DEF_HELPER_FLAGS_2(div_i32, TCG_CALL_NO_RWG_SE, s32, s32, s32)
DEF_HELPER_FLAGS_2(rem_i32, TCG_CALL_NO_RWG_SE, s32, s32, s32)
DEF_HELPER_FLAGS_2(divu_i32, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(remu_i32, TCG_CALL_NO_RWG_SE, i32, i32, i32)

DEF_HELPER_FLAGS_2(div_i64, TCG_CALL_NO_RWG_SE, s64, s64, s64)
DEF_HELPER_FLAGS_2(rem_i64, TCG_CALL_NO_RWG_SE, s64, s64, s64)
DEF_HELPER_FLAGS_2(divu_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(remu_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)

DEF_HELPER_FLAGS_2(shl_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(shr_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(sar_i64, TCG_CALL_NO_RWG_SE, s64, s64, s64)

DEF_HELPER_FLAGS_2(mulsh_i64, TCG_CALL_NO_RWG_SE, s64, s64, s64)
DEF_HELPER_FLAGS_2(muluh_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)

DEF_HELPER_FLAGS_2(clz_i32, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(ctz_i32, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(clz_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(ctz_i64, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_1(clrsb_i32, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(clrsb_i64, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_1(ctpop_i32, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(ctpop_i64, TCG_CALL_NO_RWG_SE, i64, i64)

DEF_HELPER_FLAGS_1(lookup_tb_ptr, TCG_CALL_NO_WG_SE, cptr, env)

DEF_HELPER_FLAGS_1(exit_atomic, TCG_CALL_NO_WG, noreturn, env)

#ifndef IN_HELPER_PROTO
/*
 * Pass calls to memset directly to libc, without a thunk in qemu.
 * Do not re-declare memset, especially since we fudge the type here;
 * we assume sizeof(void *) == sizeof(size_t), which is true for
 * all supported hosts.
 */
#define helper_memset memset
DEF_HELPER_FLAGS_3(memset, TCG_CALL_NO_RWG, ptr, ptr, int, ptr)
#endif /* IN_HELPER_PROTO */

DEF_HELPER_FLAGS_5(atomic_cmpxchgb, TCG_CALL_NO_WG,
                   i32, env, tl, i32, i32, i32)
DEF_HELPER_FLAGS_5(atomic_cmpxchgw_be, TCG_CALL_NO_WG,
                   i32, env, tl, i32, i32, i32)
DEF_HELPER_FLAGS_5(atomic_cmpxchgw_le, TCG_CALL_NO_WG,
                   i32, env, tl, i32, i32, i32)
DEF_HELPER_FLAGS_5(atomic_cmpxchgl_be, TCG_CALL_NO_WG,
                   i32, env, tl, i32, i32, i32)
DEF_HELPER_FLAGS_5(atomic_cmpxchgl_le, TCG_CALL_NO_WG,
                   i32, env, tl, i32, i32, i32)
#ifdef CONFIG_ATOMIC64
DEF_HELPER_FLAGS_5(atomic_cmpxchgq_be, TCG_CALL_NO_WG,
                   i64, env, tl, i64, i64, i32)
DEF_HELPER_FLAGS_5(atomic_cmpxchgq_le, TCG_CALL_NO_WG,
                   i64, env, tl, i64, i64, i32)
#endif
#ifdef CONFIG_CMPXCHG128
DEF_HELPER_FLAGS_5(atomic_cmpxchgo_be, TCG_CALL_NO_WG,
                   i128, env, tl, i128, i128, i32)
DEF_HELPER_FLAGS_5(atomic_cmpxchgo_le, TCG_CALL_NO_WG,
                   i128, env, tl, i128, i128, i32)
#endif

DEF_HELPER_FLAGS_5(nonatomic_cmpxchgo_be, TCG_CALL_NO_WG,
                   i128, env, tl, i128, i128, i32)
DEF_HELPER_FLAGS_5(nonatomic_cmpxchgo_le, TCG_CALL_NO_WG,
                   i128, env, tl, i128, i128, i32)

#ifdef CONFIG_ATOMIC64
#define GEN_ATOMIC_HELPERS(NAME)                                  \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), b),              \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), w_le),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), w_be),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), l_le),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), l_be),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), q_le),           \
                       TCG_CALL_NO_WG, i64, env, tl, i64, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), q_be),           \
                       TCG_CALL_NO_WG, i64, env, tl, i64, i32)
#else
#define GEN_ATOMIC_HELPERS(NAME)                                  \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), b),              \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), w_le),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), w_be),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), l_le),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)    \
    DEF_HELPER_FLAGS_4(glue(glue(atomic_, NAME), l_be),           \
                       TCG_CALL_NO_WG, i32, env, tl, i32, i32)
#endif /* CONFIG_ATOMIC64 */

GEN_ATOMIC_HELPERS(fetch_add)
GEN_ATOMIC_HELPERS(fetch_and)
GEN_ATOMIC_HELPERS(fetch_or)
GEN_ATOMIC_HELPERS(fetch_xor)
GEN_ATOMIC_HELPERS(fetch_smin)
GEN_ATOMIC_HELPERS(fetch_umin)
GEN_ATOMIC_HELPERS(fetch_smax)
GEN_ATOMIC_HELPERS(fetch_umax)

GEN_ATOMIC_HELPERS(add_fetch)
GEN_ATOMIC_HELPERS(and_fetch)
GEN_ATOMIC_HELPERS(or_fetch)
GEN_ATOMIC_HELPERS(xor_fetch)
GEN_ATOMIC_HELPERS(smin_fetch)
GEN_ATOMIC_HELPERS(umin_fetch)
GEN_ATOMIC_HELPERS(smax_fetch)
GEN_ATOMIC_HELPERS(umax_fetch)

GEN_ATOMIC_HELPERS(xchg)

#undef GEN_ATOMIC_HELPERS

DEF_HELPER_FLAGS_3(gvec_mov, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_dup8, TCG_CALL_NO_RWG, void, ptr, i32, i32)
DEF_HELPER_FLAGS_3(gvec_dup16, TCG_CALL_NO_RWG, void, ptr, i32, i32)
DEF_HELPER_FLAGS_3(gvec_dup32, TCG_CALL_NO_RWG, void, ptr, i32, i32)
DEF_HELPER_FLAGS_3(gvec_dup64, TCG_CALL_NO_RWG, void, ptr, i32, i64)

DEF_HELPER_FLAGS_4(gvec_add8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_add16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_add32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_add64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_adds8, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_adds16, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_adds32, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_adds64, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(gvec_sub8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sub16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sub32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sub64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_subs8, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_subs16, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_subs32, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_subs64, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(gvec_mul8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_mul16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_mul32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_mul64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_muls8, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_muls16, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_muls32, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_muls64, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(gvec_ssadd8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ssadd16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ssadd32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ssadd64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_sssub8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sssub16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sssub32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sssub64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_usadd8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_usadd16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_usadd32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_usadd64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_ussub8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ussub16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ussub32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ussub64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_smin8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smin16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smin32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smin64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_smax8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smax16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smax32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smax64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_umin8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umin16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umin32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umin64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_umax8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umax16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umax32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umax64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_neg8, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_neg16, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_neg32, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_neg64, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_abs8, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_abs16, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_abs32, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_abs64, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_not, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_and, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_or, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_xor, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_andc, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_orc, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_nand, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_nor, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_eqv, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_ands, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_xors, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(gvec_ors, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_3(gvec_shl8i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_shl16i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_shl32i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_shl64i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_shr8i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_shr16i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_shr32i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_shr64i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_sar8i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sar16i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sar32i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sar64i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_rotl8i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_rotl16i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_rotl32i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_rotl64i, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_shl8v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_shl16v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_shl32v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_shl64v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_shr8v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_shr16v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_shr32v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_shr64v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_sar8v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sar16v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sar32v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sar64v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_rotl8v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_rotl16v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_rotl32v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_rotl64v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_rotr8v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_rotr16v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_rotr32v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_rotr64v, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_eq8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_eq16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_eq32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_eq64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_ne8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ne16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ne32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ne64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_lt8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_lt16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_lt32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_lt64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_le8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_le16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_le32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_le64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_ltu8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ltu16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ltu32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ltu64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_leu8, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_leu16, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_leu32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_leu64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_bitsel, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
