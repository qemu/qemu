/*
 *  Software MMU support
 *
 * Generate inline load/store functions for all MMU modes (typically
 * at least _user and _kernel) as well as _data versions, for all data
 * sizes.
 *
 * Used by target op helpers.
 *
 * MMU mode suffixes are defined in target cpu.h.
 */

/* XXX: find something cleaner.
 * Furthermore, this is false for 64 bits targets
 */
#define ldul_user       ldl_user
#define ldul_kernel     ldl_kernel
#define ldul_hypv       ldl_hypv
#define ldul_executive  ldl_executive
#define ldul_supervisor ldl_supervisor

/* The memory helpers for tcg-generated code need tcg_target_long etc.  */
#include "tcg.h"

#define ACCESS_TYPE 0
#define MEMSUFFIX MMU_MODE0_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ACCESS_TYPE 1
#define MEMSUFFIX MMU_MODE1_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#if (NB_MMU_MODES >= 3)

#define ACCESS_TYPE 2
#define MEMSUFFIX MMU_MODE2_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 3) */

#if (NB_MMU_MODES >= 4)

#define ACCESS_TYPE 3
#define MEMSUFFIX MMU_MODE3_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 4) */

#if (NB_MMU_MODES >= 5)

#define ACCESS_TYPE 4
#define MEMSUFFIX MMU_MODE4_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 5) */

#if (NB_MMU_MODES >= 6)

#define ACCESS_TYPE 5
#define MEMSUFFIX MMU_MODE5_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 6) */

#if (NB_MMU_MODES > 6)
#error "NB_MMU_MODES > 6 is not supported for now"
#endif /* (NB_MMU_MODES > 6) */

/* these access are slower, they must be as rare as possible */
#define ACCESS_TYPE (NB_MMU_MODES)
#define MEMSUFFIX _data
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ldub(p) ldub_data(p)
#define ldsb(p) ldsb_data(p)
#define lduw(p) lduw_data(p)
#define ldsw(p) ldsw_data(p)
#define ldl(p) ldl_data(p)
#define ldq(p) ldq_data(p)

#define stb(p, v) stb_data(p, v)
#define stw(p, v) stw_data(p, v)
#define stl(p, v) stl_data(p, v)
#define stq(p, v) stq_data(p, v)
