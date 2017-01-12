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

DEF_HELPER_FLAGS_1(exit_atomic, TCG_CALL_NO_WG, noreturn, env)

#ifdef CONFIG_SOFTMMU

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

#else

DEF_HELPER_FLAGS_4(atomic_cmpxchgb, TCG_CALL_NO_WG, i32, env, tl, i32, i32)
DEF_HELPER_FLAGS_4(atomic_cmpxchgw_be, TCG_CALL_NO_WG, i32, env, tl, i32, i32)
DEF_HELPER_FLAGS_4(atomic_cmpxchgw_le, TCG_CALL_NO_WG, i32, env, tl, i32, i32)
DEF_HELPER_FLAGS_4(atomic_cmpxchgl_be, TCG_CALL_NO_WG, i32, env, tl, i32, i32)
DEF_HELPER_FLAGS_4(atomic_cmpxchgl_le, TCG_CALL_NO_WG, i32, env, tl, i32, i32)
#ifdef CONFIG_ATOMIC64
DEF_HELPER_FLAGS_4(atomic_cmpxchgq_be, TCG_CALL_NO_WG, i64, env, tl, i64, i64)
DEF_HELPER_FLAGS_4(atomic_cmpxchgq_le, TCG_CALL_NO_WG, i64, env, tl, i64, i64)
#endif

#ifdef CONFIG_ATOMIC64
#define GEN_ATOMIC_HELPERS(NAME)                             \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), b),         \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), w_le),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), w_be),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), l_le),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), l_be),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), q_le),      \
                       TCG_CALL_NO_WG, i64, env, tl, i64)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), q_be),      \
                       TCG_CALL_NO_WG, i64, env, tl, i64)
#else
#define GEN_ATOMIC_HELPERS(NAME)                             \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), b),         \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), w_le),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), w_be),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), l_le),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)    \
    DEF_HELPER_FLAGS_3(glue(glue(atomic_, NAME), l_be),      \
                       TCG_CALL_NO_WG, i32, env, tl, i32)
#endif /* CONFIG_ATOMIC64 */

#endif /* CONFIG_SOFTMMU */

GEN_ATOMIC_HELPERS(fetch_add)
GEN_ATOMIC_HELPERS(fetch_and)
GEN_ATOMIC_HELPERS(fetch_or)
GEN_ATOMIC_HELPERS(fetch_xor)

GEN_ATOMIC_HELPERS(add_fetch)
GEN_ATOMIC_HELPERS(and_fetch)
GEN_ATOMIC_HELPERS(or_fetch)
GEN_ATOMIC_HELPERS(xor_fetch)

GEN_ATOMIC_HELPERS(xchg)

#undef GEN_ATOMIC_HELPERS
