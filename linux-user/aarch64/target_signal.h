#ifndef AARCH64_TARGET_SIGNAL_H
#define AARCH64_TARGET_SIGNAL_H

#include "../generic/signal.h"

#define TARGET_SA_RESTORER      0x04000000

#define TARGET_SEGV_MTEAERR  8  /* Asynchronous ARM MTE error */
#define TARGET_SEGV_MTESERR  9  /* Synchronous ARM MTE exception */
#define TARGET_SEGV_CPERR   10  /* Control protection fault */

#define TARGET_ARCH_HAS_SETUP_FRAME
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 1

#endif /* AARCH64_TARGET_SIGNAL_H */
