/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands info structures for tcg helpers.
 * Define HELPER_H for the header file to be expanded.
 */

#include "tcg/tcg.h"
#include "tcg/helper-info.h"
#include "exec/helper-head.h"

/*
 * Need one more level of indirection before stringification
 * to get all the macros expanded first.
 */
#define str(s) #s

#define DEF_HELPER_FLAGS_0(NAME, FLAGS, RET)                            \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0)                                 \
    };

#define DEF_HELPER_FLAGS_1(NAME, FLAGS, RET, T1)                        \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
    };

#define DEF_HELPER_FLAGS_2(NAME, FLAGS, RET, T1, T2)                    \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
                  | dh_typemask(T2, 2)                                  \
    };

#define DEF_HELPER_FLAGS_3(NAME, FLAGS, RET, T1, T2, T3)                \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
                  | dh_typemask(T2, 2) | dh_typemask(T3, 3)             \
    };

#define DEF_HELPER_FLAGS_4(NAME, FLAGS, RET, T1, T2, T3, T4)            \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
                  | dh_typemask(T2, 2) | dh_typemask(T3, 3)             \
                  | dh_typemask(T4, 4)                                  \
    };

#define DEF_HELPER_FLAGS_5(NAME, FLAGS, RET, T1, T2, T3, T4, T5)        \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
                  | dh_typemask(T2, 2) | dh_typemask(T3, 3)             \
                  | dh_typemask(T4, 4) | dh_typemask(T5, 5)             \
    };

#define DEF_HELPER_FLAGS_6(NAME, FLAGS, RET, T1, T2, T3, T4, T5, T6)    \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
                  | dh_typemask(T2, 2) | dh_typemask(T3, 3)             \
                  | dh_typemask(T4, 4) | dh_typemask(T5, 5)             \
                  | dh_typemask(T6, 6)                                  \
    };

#define DEF_HELPER_FLAGS_7(NAME, FLAGS, RET, T1, T2, T3, T4, T5, T6, T7) \
    TCGHelperInfo glue(helper_info_, NAME) = {                          \
        .func = HELPER(NAME), .name = str(NAME),                        \
        .flags = FLAGS | dh_callflag(RET),                              \
        .typemask = dh_typemask(RET, 0) | dh_typemask(T1, 1)            \
                  | dh_typemask(T2, 2) | dh_typemask(T3, 3)             \
                  | dh_typemask(T4, 4) | dh_typemask(T5, 5)             \
                  | dh_typemask(T6, 6) | dh_typemask(T7, 7)             \
    };

#include HELPER_H

#undef str
#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef DEF_HELPER_FLAGS_6
#undef DEF_HELPER_FLAGS_7
