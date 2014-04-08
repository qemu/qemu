/* Helper file for declaring TCG helper functions.
   This one expands generation functions for tcg opcodes.  */

#ifndef HELPER_GEN_H
#define HELPER_GEN_H 1

#include <exec/helper-head.h>

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

#include "helper.h"
#include "tcg-runtime.h"

#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef GEN_HELPER

#endif /* HELPER_GEN_H */
