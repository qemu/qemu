#ifndef SPARC_TARGET_RESOURCE_H
#define SPARC_TARGET_RESOURCE_H

#include "../generic/target_resource.h"

#if TARGET_ABI_BITS == 32
#undef TARGET_RLIM_INFINITY
#define TARGET_RLIM_INFINITY    0x7fffffffUL
#endif

#undef TARGET_RLIMIT_NOFILE
#define TARGET_RLIMIT_NOFILE    6

#undef TARGET_RLIMIT_NPROC
#define TARGET_RLIMIT_NPROC     7

#endif
