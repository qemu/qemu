/* Helper file for declaring TCG helper functions.
   This one defines data structures private to tcg.c.  */

#ifndef HELPER_TCG_H
#define HELPER_TCG_H

#include "exec/helper-head.h"

/* Need one more level of indirection before stringification
   to get all the macros expanded first.  */
#define str(s) #s

#define DEF_HELPER_FLAGS_0(NAME, FLAGS, ret) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) },

#define DEF_HELPER_FLAGS_1(NAME, FLAGS, ret, t1) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) },

#define DEF_HELPER_FLAGS_2(NAME, FLAGS, ret, t1, t2) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) \
    | dh_typemask(t2, 2) },

#define DEF_HELPER_FLAGS_3(NAME, FLAGS, ret, t1, t2, t3) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) \
    | dh_typemask(t2, 2) | dh_typemask(t3, 3) },

#define DEF_HELPER_FLAGS_4(NAME, FLAGS, ret, t1, t2, t3, t4) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) \
    | dh_typemask(t2, 2) | dh_typemask(t3, 3) | dh_typemask(t4, 4) },

#define DEF_HELPER_FLAGS_5(NAME, FLAGS, ret, t1, t2, t3, t4, t5) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) \
    | dh_typemask(t2, 2) | dh_typemask(t3, 3) | dh_typemask(t4, 4) \
    | dh_typemask(t5, 5) },

#define DEF_HELPER_FLAGS_6(NAME, FLAGS, ret, t1, t2, t3, t4, t5, t6) \
  { .func = HELPER(NAME), .name = str(NAME), \
    .flags = FLAGS | dh_callflag(ret), \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) \
    | dh_typemask(t2, 2) | dh_typemask(t3, 3) | dh_typemask(t4, 4) \
    | dh_typemask(t5, 5) | dh_typemask(t6, 6) },

#define DEF_HELPER_FLAGS_7(NAME, FLAGS, ret, t1, t2, t3, t4, t5, t6, t7) \
  { .func = HELPER(NAME), .name = str(NAME), .flags = FLAGS, \
    .typemask = dh_typemask(ret, 0) | dh_typemask(t1, 1) \
    | dh_typemask(t2, 2) | dh_typemask(t3, 3) | dh_typemask(t4, 4) \
    | dh_typemask(t5, 5) | dh_typemask(t6, 6) | dh_typemask(t7, 7) },

#include "helper.h"
#include "accel/tcg/tcg-runtime.h"
#include "accel/tcg/plugin-helpers.h"

#undef str
#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef DEF_HELPER_FLAGS_6
#undef DEF_HELPER_FLAGS_7

#endif /* HELPER_TCG_H */
