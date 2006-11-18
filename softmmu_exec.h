/* Common softmmu definitions and inline routines.  */

#define ldul_user ldl_user
#define ldul_kernel ldl_kernel

#define ACCESS_TYPE 0
#define MEMSUFFIX _kernel
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ACCESS_TYPE 1
#define MEMSUFFIX _user
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

/* these access are slower, they must be as rare as possible */
#define ACCESS_TYPE 2
#define MEMSUFFIX _data
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
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
