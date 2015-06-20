/* Helper file for declaring TCG helper functions.
   Should be included at the start and end of target-foo/helper.h.

   Targets should use DEF_HELPER_N and DEF_HELPER_FLAGS_N to declare helper
   functions.  Names should be specified without the helper_ prefix, and
   the return and argument types specified.  3 basic types are understood
   (i32, i64 and ptr).  Additional aliases are provided for convenience and
   to match the types used by the C helper implementation.

   The target helper.h should be included in all files that use/define
   helper functions.  THis will ensure that function prototypes are
   consistent.  In addition it should be included an extra two times for
   helper.c, defining:
    GEN_HELPER 1 to produce op generation functions (gen_helper_*)
    GEN_HELPER 2 to do runtime registration helper functions.
 */

#ifndef DEF_HELPER_H
#define DEF_HELPER_H 1

#define HELPER(name) glue(helper_, name)

#define GET_TCGV_i32 GET_TCGV_I32
#define GET_TCGV_i64 GET_TCGV_I64
#define GET_TCGV_ptr GET_TCGV_PTR

/* Some types that make sense in C, but not for TCG.  */
#define dh_alias_i32 i32
#define dh_alias_s32 i32
#define dh_alias_int i32
#define dh_alias_i64 i64
#define dh_alias_s64 i64
#define dh_alias_f32 i32
#define dh_alias_f64 i64
#if TARGET_LONG_BITS == 32
#define dh_alias_tl i32
#else
#define dh_alias_tl i64
#endif
#define dh_alias_ptr ptr
#define dh_alias_void void
#define dh_alias_noreturn noreturn
#define dh_alias_env ptr
#define dh_alias(t) glue(dh_alias_, t)

#define dh_ctype_i32 uint32_t
#define dh_ctype_s32 int32_t
#define dh_ctype_int int
#define dh_ctype_i64 uint64_t
#define dh_ctype_s64 int64_t
#define dh_ctype_f32 float32
#define dh_ctype_f64 float64
#define dh_ctype_tl target_ulong
#define dh_ctype_ptr void *
#define dh_ctype_void void
#define dh_ctype_noreturn void QEMU_NORETURN
#define dh_ctype_env CPUArchState *
#define dh_ctype(t) dh_ctype_##t

/* We can't use glue() here because it falls foul of C preprocessor
   recursive expansion rules.  */
#define dh_retvar_decl0_void void
#define dh_retvar_decl0_noreturn void
#define dh_retvar_decl0_i32 TCGv_i32 retval
#define dh_retvar_decl0_i64 TCGv_i64 retval
#define dh_retvar_decl0_ptr TCGv_ptr retval
#define dh_retvar_decl0(t) glue(dh_retvar_decl0_, dh_alias(t))

#define dh_retvar_decl_void
#define dh_retvar_decl_noreturn
#define dh_retvar_decl_i32 TCGv_i32 retval,
#define dh_retvar_decl_i64 TCGv_i64 retval,
#define dh_retvar_decl_ptr TCGv_ptr retval,
#define dh_retvar_decl(t) glue(dh_retvar_decl_, dh_alias(t))

#define dh_retvar_void TCG_CALL_DUMMY_ARG
#define dh_retvar_noreturn TCG_CALL_DUMMY_ARG
#define dh_retvar_i32 GET_TCGV_i32(retval)
#define dh_retvar_i64 GET_TCGV_i64(retval)
#define dh_retvar_ptr GET_TCGV_ptr(retval)
#define dh_retvar(t) glue(dh_retvar_, dh_alias(t))

#define dh_is_64bit_void 0
#define dh_is_64bit_noreturn 0
#define dh_is_64bit_i32 0
#define dh_is_64bit_i64 1
#define dh_is_64bit_ptr (TCG_TARGET_REG_BITS == 64)
#define dh_is_64bit(t) glue(dh_is_64bit_, dh_alias(t))

#define dh_is_signed_void 0
#define dh_is_signed_noreturn 0
#define dh_is_signed_i32 0
#define dh_is_signed_s32 1
#define dh_is_signed_i64 0
#define dh_is_signed_s64 1
#define dh_is_signed_f32 0
#define dh_is_signed_f64 0
#define dh_is_signed_tl  0
#define dh_is_signed_int 1
/* ??? This is highly specific to the host cpu.  There are even special
   extension instructions that may be required, e.g. ia64's addp4.  But
   for now we don't support any 64-bit targets with 32-bit pointers.  */
#define dh_is_signed_ptr 0
#define dh_is_signed_env dh_is_signed_ptr
#define dh_is_signed(t) dh_is_signed_##t

#define dh_sizemask(t, n) \
  sizemask |= dh_is_64bit(t) << (n*2); \
  sizemask |= dh_is_signed(t) << (n*2+1)

#define dh_arg(t, n) \
  args[n - 1] = glue(GET_TCGV_, dh_alias(t))(glue(arg, n)); \
  dh_sizemask(t, n)

#define dh_arg_decl(t, n) glue(TCGv_, dh_alias(t)) glue(arg, n)


#define DEF_HELPER_0(name, ret) \
    DEF_HELPER_FLAGS_0(name, 0, ret)
#define DEF_HELPER_1(name, ret, t1) \
    DEF_HELPER_FLAGS_1(name, 0, ret, t1)
#define DEF_HELPER_2(name, ret, t1, t2) \
    DEF_HELPER_FLAGS_2(name, 0, ret, t1, t2)
#define DEF_HELPER_3(name, ret, t1, t2, t3) \
    DEF_HELPER_FLAGS_3(name, 0, ret, t1, t2, t3)
#define DEF_HELPER_4(name, ret, t1, t2, t3, t4) \
    DEF_HELPER_FLAGS_4(name, 0, ret, t1, t2, t3, t4)
#define DEF_HELPER_5(name, ret, t1, t2, t3, t4, t5) \
    DEF_HELPER_FLAGS_5(name, 0, ret, t1, t2, t3, t4, t5)

/* MAX_OPC_PARAM_IARGS must be set to n if last entry is DEF_HELPER_FLAGS_n. */

#endif /* DEF_HELPER_H */

#ifndef GEN_HELPER
/* Function prototypes.  */

#define DEF_HELPER_FLAGS_0(name, flags, ret) \
dh_ctype(ret) HELPER(name) (void);

#define DEF_HELPER_FLAGS_1(name, flags, ret, t1) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1));

#define DEF_HELPER_FLAGS_2(name, flags, ret, t1, t2) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2));

#define DEF_HELPER_FLAGS_3(name, flags, ret, t1, t2, t3) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3));

#define DEF_HELPER_FLAGS_4(name, flags, ret, t1, t2, t3, t4) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3), \
                                   dh_ctype(t4));

#define DEF_HELPER_FLAGS_5(name, flags, ret, t1, t2, t3, t4, t5) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3), \
                            dh_ctype(t4), dh_ctype(t5));

#undef GEN_HELPER
#define GEN_HELPER -1

#elif GEN_HELPER == 1
/* Gen functions.  */

#define DEF_HELPER_FLAGS_0(name, flags, ret) \
static inline void glue(gen_helper_, name)(dh_retvar_decl0(ret)) \
{ \
  int sizemask; \
  sizemask = dh_is_64bit(ret); \
  tcg_gen_helperN(HELPER(name), flags, sizemask, dh_retvar(ret), 0, NULL); \
}

#define DEF_HELPER_FLAGS_1(name, flags, ret, t1) \
static inline void glue(gen_helper_, name)(dh_retvar_decl(ret) dh_arg_decl(t1, 1)) \
{ \
  TCGArg args[1]; \
  int sizemask = 0; \
  dh_sizemask(ret, 0); \
  dh_arg(t1, 1); \
  tcg_gen_helperN(HELPER(name), flags, sizemask, dh_retvar(ret), 1, args); \
}

#define DEF_HELPER_FLAGS_2(name, flags, ret, t1, t2) \
static inline void glue(gen_helper_, name)(dh_retvar_decl(ret) dh_arg_decl(t1, 1), \
    dh_arg_decl(t2, 2)) \
{ \
  TCGArg args[2]; \
  int sizemask = 0; \
  dh_sizemask(ret, 0); \
  dh_arg(t1, 1); \
  dh_arg(t2, 2); \
  tcg_gen_helperN(HELPER(name), flags, sizemask, dh_retvar(ret), 2, args); \
}

#define DEF_HELPER_FLAGS_3(name, flags, ret, t1, t2, t3) \
static inline void glue(gen_helper_, name)(dh_retvar_decl(ret) dh_arg_decl(t1, 1), \
    dh_arg_decl(t2, 2), dh_arg_decl(t3, 3)) \
{ \
  TCGArg args[3]; \
  int sizemask = 0; \
  dh_sizemask(ret, 0); \
  dh_arg(t1, 1); \
  dh_arg(t2, 2); \
  dh_arg(t3, 3); \
  tcg_gen_helperN(HELPER(name), flags, sizemask, dh_retvar(ret), 3, args); \
}

#define DEF_HELPER_FLAGS_4(name, flags, ret, t1, t2, t3, t4) \
static inline void glue(gen_helper_, name)(dh_retvar_decl(ret) dh_arg_decl(t1, 1), \
    dh_arg_decl(t2, 2), dh_arg_decl(t3, 3), dh_arg_decl(t4, 4)) \
{ \
  TCGArg args[4]; \
  int sizemask = 0; \
  dh_sizemask(ret, 0); \
  dh_arg(t1, 1); \
  dh_arg(t2, 2); \
  dh_arg(t3, 3); \
  dh_arg(t4, 4); \
  tcg_gen_helperN(HELPER(name), flags, sizemask, dh_retvar(ret), 4, args); \
}

#define DEF_HELPER_FLAGS_5(name, flags, ret, t1, t2, t3, t4, t5) \
static inline void glue(gen_helper_, name)(dh_retvar_decl(ret) \
    dh_arg_decl(t1, 1),  dh_arg_decl(t2, 2), dh_arg_decl(t3, 3), \
    dh_arg_decl(t4, 4), dh_arg_decl(t5, 5)) \
{ \
  TCGArg args[5]; \
  int sizemask = 0; \
  dh_sizemask(ret, 0); \
  dh_arg(t1, 1); \
  dh_arg(t2, 2); \
  dh_arg(t3, 3); \
  dh_arg(t4, 4); \
  dh_arg(t5, 5); \
  tcg_gen_helperN(HELPER(name), flags, sizemask, dh_retvar(ret), 5, args); \
}

#undef GEN_HELPER
#define GEN_HELPER -1

#elif GEN_HELPER == 2
/* Register helpers.  */

#define DEF_HELPER_FLAGS_0(name, flags, ret)  { HELPER(name), #name },

#define DEF_HELPER_FLAGS_1(name, flags, ret, t1) \
DEF_HELPER_FLAGS_0(name, flags, ret)

#define DEF_HELPER_FLAGS_2(name, flags, ret, t1, t2) \
DEF_HELPER_FLAGS_0(name, flags, ret)

#define DEF_HELPER_FLAGS_3(name, flags, ret, t1, t2, t3) \
DEF_HELPER_FLAGS_0(name, flags, ret)

#define DEF_HELPER_FLAGS_4(name, flags, ret, t1, t2, t3, t4) \
DEF_HELPER_FLAGS_0(name, flags, ret)

#define DEF_HELPER_FLAGS_5(name, flags, ret, t1, t2, t3, t4, t5) \
DEF_HELPER_FLAGS_0(name, flags, ret)

#undef GEN_HELPER
#define GEN_HELPER -1

#elif GEN_HELPER == -1
/* Undefine macros.  */

#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef GEN_HELPER

#endif
