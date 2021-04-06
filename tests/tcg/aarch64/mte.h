/*
 * Linux kernel fallback API definitions for MTE and test helpers.
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#ifndef PR_SET_TAGGED_ADDR_CTRL
# define PR_SET_TAGGED_ADDR_CTRL  55
#endif
#ifndef PR_TAGGED_ADDR_ENABLE
# define PR_TAGGED_ADDR_ENABLE    (1UL << 0)
#endif
#ifndef PR_MTE_TCF_SHIFT
# define PR_MTE_TCF_SHIFT         1
# define PR_MTE_TCF_NONE          (0UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_SYNC          (1UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_ASYNC         (2UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TAG_SHIFT         3
#endif

#ifndef PROT_MTE
# define PROT_MTE 0x20
#endif

#ifndef SEGV_MTEAERR
# define SEGV_MTEAERR    8
# define SEGV_MTESERR    9
#endif

static void enable_mte(int tcf)
{
    int r = prctl(PR_SET_TAGGED_ADDR_CTRL,
                  PR_TAGGED_ADDR_ENABLE | tcf | (0xfffe << PR_MTE_TAG_SHIFT),
                  0, 0, 0);
    if (r < 0) {
        perror("PR_SET_TAGGED_ADDR_CTRL");
        exit(2);
    }
}

static void * alloc_mte_mem(size_t size) __attribute__((unused));
static void * alloc_mte_mem(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_MTE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap PROT_MTE");
        exit(2);
    }
    return p;
}
