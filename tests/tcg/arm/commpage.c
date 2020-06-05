/*
 * Verify the COMMPAGE emulation
 *
 * The ARM commpage is a set of user space helper functions provided
 * by the kernel in an effort to ease portability of user space code
 * between different CPUs with potentially different capabilities. It
 * is a 32 bit invention and similar to the vdso segment in many ways.
 *
 * The ABI is documented in the Linux kernel:
 *     Documentation/arm/kernel_userspace_helpers.rst
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define ARM_COMMPAGE      (0xffff0f00u)
#define ARM_KUSER_VERSION (*(int32_t *)(ARM_COMMPAGE + 0xfc))
typedef void * (get_tls_fn)(void);
#define ARM_KUSER_GET_TLS (*(get_tls_fn *)(ARM_COMMPAGE + 0xe0))
typedef int (cmpxchg_fn)(int oldval, int newval, volatile int *ptr);
#define ARM_KUSER_CMPXCHG (*(cmpxchg_fn *)(ARM_COMMPAGE + 0xc0))
typedef void (dmb_fn)(void);
#define ARM_KUSER_DMB (*(dmb_fn *)(ARM_COMMPAGE + 0xa0))
typedef int (cmpxchg64_fn)(const int64_t *oldval,
                           const int64_t *newval,
                           volatile int64_t *ptr);
#define ARM_KUSER_CMPXCHG64 (*(cmpxchg64_fn *)(ARM_COMMPAGE + 0x60))

#define fail_unless(x)                                                  \
    do {                                                                \
        if (!(x)) {                                                     \
            fprintf(stderr, "FAILED at %s:%d\n", __FILE__, __LINE__);   \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)


int main(int argc, char *argv[argc])
{
    void *kuser_tls;
    int val = 1;
    const int64_t oldval = 1, newval = 2;
    int64_t val64 = 1;

    fail_unless(ARM_KUSER_VERSION == 0x5);
    kuser_tls = ARM_KUSER_GET_TLS();
    printf("TLS = %p\n", kuser_tls);
    fail_unless(kuser_tls != 0);
    fail_unless(ARM_KUSER_CMPXCHG(1, 2, &val) == 0);
    printf("val = %d\n", val);
    /* this is a crash test, not checking an actual barrier occurs */
    ARM_KUSER_DMB();
    fail_unless(ARM_KUSER_CMPXCHG64(&oldval, &newval, &val64) == 0);
    printf("val64 = %lld\n", val64);
    return 0;
}
