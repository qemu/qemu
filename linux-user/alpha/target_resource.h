#ifndef ALPHA_TARGET_RESOURCE_H
#define ALPHA_TARGET_RESOURCE_H

#include "../generic/target_resource.h"

#undef TARGET_RLIM_INFINITY
#define TARGET_RLIM_INFINITY    0x7fffffffffffffffull

#undef TARGET_RLIMIT_NOFILE
#define TARGET_RLIMIT_NOFILE    6

#undef TARGET_RLIMIT_AS
#define TARGET_RLIMIT_AS        7

#undef TARGET_RLIMIT_NPROC
#define TARGET_RLIMIT_NPROC     8

#undef TARGET_RLIMIT_MEMLOCK
#define TARGET_RLIMIT_MEMLOCK   9

#endif
