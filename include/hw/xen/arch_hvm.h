#if defined(TARGET_I386) || defined(TARGET_X86_64)
#include "hw/i386/xen_arch_hvm.h"
#elif defined(TARGET_ARM) || defined(TARGET_ARM_64)
#include "hw/arm/xen_arch_hvm.h"
#endif
