#ifndef X86_64_TARGET_SIGNAL_H
#define X86_64_TARGET_SIGNAL_H

#include "../generic/signal.h"

/* For x86_64, use of SA_RESTORER is mandatory. */
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 0

#endif /* X86_64_TARGET_SIGNAL_H */
