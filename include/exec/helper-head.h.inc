/*
 * Helper file for declaring TCG helper functions.
 * Used by other helper files.
 */

#ifndef EXEC_HELPER_HEAD_H
#define EXEC_HELPER_HEAD_H

#include "fpu/softfloat-types.h"

#define HELPER(name) glue(helper_, name)

/* Some types that make sense in C, but not for TCG.  */
#define dh_alias_i32 i32
#define dh_alias_s32 i32
#define dh_alias_int i32
#define dh_alias_i64 i64
#define dh_alias_s64 i64
#define dh_alias_i128 i128
#define dh_alias_f16 i32
#define dh_alias_f32 i32
#define dh_alias_f64 i64
#define dh_alias_ptr ptr
#define dh_alias_cptr ptr
#define dh_alias_env ptr
#define dh_alias_fpst ptr
#define dh_alias_void void
#define dh_alias_noreturn noreturn
#define dh_alias(t) glue(dh_alias_, t)

#define dh_ctype_i32 uint32_t
#define dh_ctype_s32 int32_t
#define dh_ctype_int int
#define dh_ctype_i64 uint64_t
#define dh_ctype_s64 int64_t
#define dh_ctype_i128 Int128
#define dh_ctype_f16 uint32_t
#define dh_ctype_f32 float32
#define dh_ctype_f64 float64
#define dh_ctype_ptr void *
#define dh_ctype_cptr const void *
#define dh_ctype_env CPUArchState *
#define dh_ctype_fpst float_status *
#define dh_ctype_void void
#define dh_ctype_noreturn G_NORETURN void
#define dh_ctype(t) dh_ctype_##t

#ifdef COMPILING_PER_TARGET
# ifdef TARGET_LONG_BITS
#  if TARGET_LONG_BITS == 32
#   define dh_alias_tl i32
#   define dh_typecode_tl dh_typecode_i32
#  else
#   define dh_alias_tl i64
#   define dh_typecode_tl dh_typecode_i64
#  endif
# endif
# define dh_ctype_tl target_ulong
#endif /* COMPILING_PER_TARGET */

#if __SIZEOF_POINTER__ == 4
# define dh_alias_vaddr i32
# define dh_typecode_vaddr dh_typecode_i32
#elif __SIZEOF_POINTER__ == 8
# define dh_alias_vaddr i64
# define dh_typecode_vaddr dh_typecode_i64
#else
# error "sizeof pointer is different from {4,8}"
#endif /* __SIZEOF_POINTER__ */
# define dh_ctype_vaddr uintptr_t

/* We can't use glue() here because it falls foul of C preprocessor
   recursive expansion rules.  */
#define dh_retvar_decl0_void void
#define dh_retvar_decl0_noreturn void
#define dh_retvar_decl0_i32 TCGv_i32 retval
#define dh_retvar_decl0_i64 TCGv_i64 retval
#define dh_retval_decl0_i128 TCGv_i128 retval
#define dh_retvar_decl0_ptr TCGv_ptr retval
#define dh_retvar_decl0(t) glue(dh_retvar_decl0_, dh_alias(t))

#define dh_retvar_decl_void
#define dh_retvar_decl_noreturn
#define dh_retvar_decl_i32 TCGv_i32 retval,
#define dh_retvar_decl_i64 TCGv_i64 retval,
#define dh_retvar_decl_i128 TCGv_i128 retval,
#define dh_retvar_decl_ptr TCGv_ptr retval,
#define dh_retvar_decl(t) glue(dh_retvar_decl_, dh_alias(t))

#define dh_retvar_void NULL
#define dh_retvar_noreturn NULL
#define dh_retvar_i32 tcgv_i32_temp(retval)
#define dh_retvar_i64 tcgv_i64_temp(retval)
#define dh_retvar_i128 tcgv_i128_temp(retval)
#define dh_retvar_ptr tcgv_ptr_temp(retval)
#define dh_retvar(t) glue(dh_retvar_, dh_alias(t))

#define dh_typecode_void 0
#define dh_typecode_noreturn 0
#define dh_typecode_i32 2
#define dh_typecode_s32 3
#define dh_typecode_i64 4
#define dh_typecode_s64 5
#define dh_typecode_ptr 6
#define dh_typecode_i128 7
#define dh_typecode_int dh_typecode_s32
#define dh_typecode_f16 dh_typecode_i32
#define dh_typecode_f32 dh_typecode_i32
#define dh_typecode_f64 dh_typecode_i64
#define dh_typecode_cptr dh_typecode_ptr
#define dh_typecode_env dh_typecode_ptr
#define dh_typecode_fpst dh_typecode_ptr
#define dh_typecode(t) dh_typecode_##t

#define dh_callflag_i32  0
#define dh_callflag_i64  0
#define dh_callflag_i128 0
#define dh_callflag_ptr  0
#define dh_callflag_void 0
#define dh_callflag_noreturn TCG_CALL_NO_RETURN
#define dh_callflag(t) glue(dh_callflag_, dh_alias(t))

#define dh_typemask(t, n)  (dh_typecode(t) << (n * 3))

#define dh_arg(t, n) \
  glue(glue(tcgv_, dh_alias(t)), _temp)(glue(arg, n))

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
#define DEF_HELPER_6(name, ret, t1, t2, t3, t4, t5, t6) \
    DEF_HELPER_FLAGS_6(name, 0, ret, t1, t2, t3, t4, t5, t6)
#define DEF_HELPER_7(name, ret, t1, t2, t3, t4, t5, t6, t7) \
    DEF_HELPER_FLAGS_7(name, 0, ret, t1, t2, t3, t4, t5, t6, t7)

/* MAX_CALL_IARGS must be set to n if last entry is DEF_HELPER_FLAGS_n. */

#endif /* EXEC_HELPER_HEAD_H */
