#ifndef PPC_TARGET_SIGNAL_H
#define PPC_TARGET_SIGNAL_H

#include "../generic/signal.h"

#if !defined(TARGET_PPC64)
#define TARGET_ARCH_HAS_SETUP_FRAME
#endif
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 1

#endif /* PPC_TARGET_SIGNAL_H */
