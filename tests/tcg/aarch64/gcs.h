/*
 * Linux kernel fallback API definitions for GCS and test helpers.
 *
 * Copyright (c) 2025 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#ifndef PR_GET_SHADOW_STACK_STATUS
#define PR_GET_SHADOW_STACK_STATUS      74
#endif
#ifndef PR_SET_SHADOW_STACK_STATUS
#define PR_SET_SHADOW_STACK_STATUS      75
#endif
#ifndef PR_LOCK_SHADOW_STACK_STATUS
#define PR_LOCK_SHADOW_STACK_STATUS     76
#endif
#ifndef PR_SHADOW_STACK_ENABLE
# define PR_SHADOW_STACK_ENABLE         (1 << 0)
# define PR_SHADOW_STACK_WRITE          (1 << 1)
# define PR_SHADOW_STACK_PUSH           (1 << 2)
#endif
#ifndef SHADOW_STACK_SET_TOKEN
#define SHADOW_STACK_SET_TOKEN          (1 << 0)
#endif
#ifndef SHADOW_STACK_SET_MARKER
#define SHADOW_STACK_SET_MARKER         (1 << 1)
#endif
#ifndef SEGV_CPERR
#define SEGV_CPERR  10
#endif
#ifndef __NR_map_shadow_stack
#define __NR_map_shadow_stack  453
#endif

/*
 * Macros, and implement the syscall inline, lest we fail
 * the checked return from any function call.
 */
#define enable_gcs(flags) \
    do {                                                                     \
        register long num  __asm__ ("x8") = __NR_prctl;                      \
        register long arg1 __asm__ ("x0") = PR_SET_SHADOW_STACK_STATUS;      \
        register long arg2 __asm__ ("x1") = PR_SHADOW_STACK_ENABLE | flags;  \
        register long arg3 __asm__ ("x2") = 0;                               \
        register long arg4 __asm__ ("x3") = 0;                               \
        register long arg5 __asm__ ("x4") = 0;                               \
        asm volatile("svc #0"                                                \
                     : "+r"(arg1)                                            \
                     : "r"(arg2), "r"(arg3), "r"(arg4), "r"(arg5), "r"(num)  \
                     : "memory", "cc");                                      \
        if (arg1) {                                                          \
            errno = -arg1;                                                   \
            perror("PR_SET_SHADOW_STACK_STATUS");                            \
            exit(2);                                                         \
        }                                                                    \
    } while (0)

#define gcspr() \
    ({ uint64_t *r; asm volatile("mrs %0, s3_3_c2_c5_1" : "=r"(r)); r; })

#define gcsss1(val) \
    do {                                                                     \
        asm volatile("sys #3, c7, c7, #2, %0" : : "r"(val) : "memory");      \
    } while (0)

#define gcsss2() \
    ({ uint64_t *r;                                                          \
       asm volatile("sysl %0, #3, c7, c7, #3" : "=r"(r) : : "memory"); r; })
