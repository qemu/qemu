/*
 * safe-syscall.S : include the host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 *
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * Copyright (C) 2016 Linaro Limited
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hostdep.h"
#include "errno_defs.h"

/* We have the correct host directory on our include path
 * so that this will pull in the right fragment for the architecture.
 */
#ifdef HAVE_SAFE_SYSCALL
#include "safe-syscall.inc.S"
#endif

/* We must specifically say that we're happy for the stack to not be
 * executable, otherwise the toolchain will default to assuming our
 * assembly needs an executable stack and the whole QEMU binary will
 * needlessly end up with one. This should be the last thing in this file.
 */
#if defined(__linux__) && defined(__ELF__)
.section        .note.GNU-stack, "", %progbits
#endif
