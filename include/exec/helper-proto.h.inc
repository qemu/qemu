/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands prototypes for the helper functions.
 * Define HELPER_H for the header file to be expanded.
 */

#include "exec/helper-head.h.inc"

/*
 * Work around an issue with --enable-lto, in which GCC's ipa-split pass
 * decides to split out the noreturn code paths that raise an exception,
 * taking the __builtin_return_address() along into the new function,
 * where it no longer computes a value that returns to TCG generated code.
 * Despite the name, the noinline attribute affects splitter, so this
 * prevents the optimization in question.  Given that helpers should not
 * otherwise be called directly, this should not have any other visible effect.
 *
 * See https://gitlab.com/qemu-project/qemu/-/issues/1454
 */
#define DEF_HELPER_ATTR  __attribute__((noinline))

#define DEF_HELPER_FLAGS_0(name, flags, ret) \
dh_ctype(ret) HELPER(name) (void) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_1(name, flags, ret, t1) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1)) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_2(name, flags, ret, t1, t2) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2)) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_3(name, flags, ret, t1, t2, t3) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), \
                            dh_ctype(t3)) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_4(name, flags, ret, t1, t2, t3, t4) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3), \
                            dh_ctype(t4)) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_5(name, flags, ret, t1, t2, t3, t4, t5) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3), \
                            dh_ctype(t4), dh_ctype(t5)) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_6(name, flags, ret, t1, t2, t3, t4, t5, t6) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3), \
                            dh_ctype(t4), dh_ctype(t5), \
                            dh_ctype(t6)) DEF_HELPER_ATTR;

#define DEF_HELPER_FLAGS_7(name, flags, ret, t1, t2, t3, t4, t5, t6, t7) \
dh_ctype(ret) HELPER(name) (dh_ctype(t1), dh_ctype(t2), dh_ctype(t3), \
                            dh_ctype(t4), dh_ctype(t5), dh_ctype(t6), \
                            dh_ctype(t7)) DEF_HELPER_ATTR;

#define IN_HELPER_PROTO

#include HELPER_H

#undef IN_HELPER_PROTO

#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef DEF_HELPER_FLAGS_6
#undef DEF_HELPER_FLAGS_7
#undef DEF_HELPER_ATTR
