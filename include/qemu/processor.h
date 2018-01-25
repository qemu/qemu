/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2.
 *   See the COPYING file in the top-level directory.
 */
#ifndef QEMU_PROCESSOR_H
#define QEMU_PROCESSOR_H

#include "qemu/atomic.h"

#if defined(__i386__) || defined(__x86_64__)
# define cpu_relax() asm volatile("rep; nop" ::: "memory")

#elif defined(__aarch64__)
# define cpu_relax() asm volatile("yield" ::: "memory")

#elif defined(__powerpc64__)
/* set Hardware Multi-Threading (HMT) priority to low; then back to medium */
# define cpu_relax() asm volatile("or 1, 1, 1;" \
                                  "or 2, 2, 2;" ::: "memory")

#else
# define cpu_relax() barrier()
#endif

#endif /* QEMU_PROCESSOR_H */
