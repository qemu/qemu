#ifdef TARGET_ABI_MIPSN32
#define TARGET_SYSCALL_OFFSET 6000
#include "syscall_n32_nr.h"
#else
#define TARGET_SYSCALL_OFFSET 5000
#include "syscall_n64_nr.h"
#endif
