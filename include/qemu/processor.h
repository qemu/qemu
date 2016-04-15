#ifndef QEMU_PROCESSOR_H
#define QEMU_PROCESSOR_H

#include "qemu/atomic.h"

#if defined(__i386__) || defined(__x86_64__)
#define cpu_relax() asm volatile("rep; nop" ::: "memory")
#endif

#ifdef __ia64__
#define cpu_relax() asm volatile("hint @pause" ::: "memory")
#endif

#ifdef __aarch64__
#define cpu_relax() asm volatile("yield" ::: "memory")
#endif

#if defined(__powerpc64__)
/* set Hardware Multi-Threading (HMT) priority to low; then back to medium */
#define cpu_relax() asm volatile("or 1, 1, 1;"
                                 "or 2, 2, 2;" ::: "memory")
#endif

#ifndef cpu_relax
#define cpu_relax() barrier()
#endif

#endif /* QEMU_PROCESSOR_H */
