/* Helper file for declaring TCG helper functions.
   This one defines data structures private to tcg.c.  */

#ifndef HELPER_TCG_H
#define HELPER_TCG_H 1

#include <exec/helper-head.h>

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

#include "helper.h"

#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5

#endif /* HELPER_TCG_H */
