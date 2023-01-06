/*
 * Test that VMA updates do not race.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Map a contiguous chunk of RWX memory. Split it into 8 equally sized
 * regions, each of which is guaranteed to have a certain combination of
 * protection bits set.
 *
 * Reader, writer and executor threads perform the respective operations on
 * pages, which are guaranteed to have the respective protection bit set.
 * Two mutator threads change the non-fixed protection bits randomly.
 */
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nop_func.h"

#define PAGE_IDX_BITS 10
#define PAGE_COUNT (1 << PAGE_IDX_BITS)
#define PAGE_IDX_MASK (PAGE_COUNT - 1)
#define REGION_IDX_BITS 3
#define PAGE_IDX_R_MASK (1 << 7)
#define PAGE_IDX_W_MASK (1 << 8)
#define PAGE_IDX_X_MASK (1 << 9)
#define REGION_MASK (PAGE_IDX_R_MASK | PAGE_IDX_W_MASK | PAGE_IDX_X_MASK)
#define PAGES_PER_REGION (1 << (PAGE_IDX_BITS - REGION_IDX_BITS))

struct context {
    int pagesize;
    char *ptr;
    int dev_null_fd;
    volatile int mutator_count;
};

static void *thread_read(void *arg)
{
    struct context *ctx = arg;
    ssize_t sret;
    size_t i, j;
    int ret;

    for (i = 0; ctx->mutator_count; i++) {
        char *p;

        j = (i & PAGE_IDX_MASK) | PAGE_IDX_R_MASK;
        p = &ctx->ptr[j * ctx->pagesize];

        /* Read directly. */
        ret = memcmp(p, nop_func, sizeof(nop_func));
        if (ret != 0) {
            fprintf(stderr, "fail direct read %p\n", p);
            abort();
        }

        /* Read indirectly. */
        sret = write(ctx->dev_null_fd, p, 1);
        if (sret != 1) {
            if (sret < 0) {
                fprintf(stderr, "fail indirect read %p (%m)\n", p);
            } else {
                fprintf(stderr, "fail indirect read %p (%zd)\n", p, sret);
            }
            abort();
        }
    }

    return NULL;
}

static void *thread_write(void *arg)
{
    struct context *ctx = arg;
    struct timespec *ts;
    size_t i, j;
    int ret;

    for (i = 0; ctx->mutator_count; i++) {
        j = (i & PAGE_IDX_MASK) | PAGE_IDX_W_MASK;

        /* Write directly. */
        memcpy(&ctx->ptr[j * ctx->pagesize], nop_func, sizeof(nop_func));

        /* Write using a syscall. */
        ts = (struct timespec *)(&ctx->ptr[(j + 1) * ctx->pagesize] -
                                 sizeof(struct timespec));
        ret = clock_gettime(CLOCK_REALTIME, ts);
        if (ret != 0) {
            fprintf(stderr, "fail indirect write %p (%m)\n", ts);
            abort();
        }
    }

    return NULL;
}

static void *thread_execute(void *arg)
{
    struct context *ctx = arg;
    size_t i, j;

    for (i = 0; ctx->mutator_count; i++) {
        j = (i & PAGE_IDX_MASK) | PAGE_IDX_X_MASK;
        ((void(*)(void))&ctx->ptr[j * ctx->pagesize])();
    }

    return NULL;
}

static void *thread_mutate(void *arg)
{
    size_t i, start_idx, end_idx, page_idx, tmp;
    struct context *ctx = arg;
    unsigned int seed;
    int prot, ret;

    seed = (unsigned int)time(NULL);
    for (i = 0; i < 10000; i++) {
        start_idx = rand_r(&seed) & PAGE_IDX_MASK;
        end_idx = rand_r(&seed) & PAGE_IDX_MASK;
        if (start_idx > end_idx) {
            tmp = start_idx;
            start_idx = end_idx;
            end_idx = tmp;
        }
        prot = rand_r(&seed) & (PROT_READ | PROT_WRITE | PROT_EXEC);
        for (page_idx = start_idx & REGION_MASK; page_idx <= end_idx;
             page_idx += PAGES_PER_REGION) {
            if (page_idx & PAGE_IDX_R_MASK) {
                prot |= PROT_READ;
            }
            if (page_idx & PAGE_IDX_W_MASK) {
                /* FIXME: qemu syscalls check for both read+write. */
                prot |= PROT_WRITE | PROT_READ;
            }
            if (page_idx & PAGE_IDX_X_MASK) {
                prot |= PROT_EXEC;
            }
        }
        ret = mprotect(&ctx->ptr[start_idx * ctx->pagesize],
                       (end_idx - start_idx + 1) * ctx->pagesize, prot);
        assert(ret == 0);
    }

    __atomic_fetch_sub(&ctx->mutator_count, 1, __ATOMIC_SEQ_CST);

    return NULL;
}

int main(void)
{
    pthread_t threads[5];
    struct context ctx;
    size_t i;
    int ret;

    /* Without a template, nothing to test. */
    if (sizeof(nop_func) == 0) {
        return EXIT_SUCCESS;
    }

    /* Initialize memory chunk. */
    ctx.pagesize = getpagesize();
    ctx.ptr = mmap(NULL, PAGE_COUNT * ctx.pagesize,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(ctx.ptr != MAP_FAILED);
    for (i = 0; i < PAGE_COUNT; i++) {
        memcpy(&ctx.ptr[i * ctx.pagesize], nop_func, sizeof(nop_func));
    }
    ctx.dev_null_fd = open("/dev/null", O_WRONLY);
    assert(ctx.dev_null_fd >= 0);
    ctx.mutator_count = 2;

    /* Start threads. */
    ret = pthread_create(&threads[0], NULL, thread_read, &ctx);
    assert(ret == 0);
    ret = pthread_create(&threads[1], NULL, thread_write, &ctx);
    assert(ret == 0);
    ret = pthread_create(&threads[2], NULL, thread_execute, &ctx);
    assert(ret == 0);
    for (i = 3; i <= 4; i++) {
        ret = pthread_create(&threads[i], NULL, thread_mutate, &ctx);
        assert(ret == 0);
    }

    /* Wait for threads to stop. */
    for (i = 0; i < sizeof(threads) / sizeof(threads[0]); i++) {
        ret = pthread_join(threads[i], NULL);
        assert(ret == 0);
    }

    /* Destroy memory chunk. */
    ret = close(ctx.dev_null_fd);
    assert(ret == 0);
    ret = munmap(ctx.ptr, PAGE_COUNT * ctx.pagesize);
    assert(ret == 0);

    return EXIT_SUCCESS;
}
