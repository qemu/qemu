#ifndef NIOS2_TARGET_SIGNAL_H
#define NIOS2_TARGET_SIGNAL_H

#include "../generic/signal.h"

/* Nios2 uses a fixed address on the kuser page for sigreturn. */
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 0

#endif /* NIOS2_TARGET_SIGNAL_H */
