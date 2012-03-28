/*
 * Coroutine tests
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include <glib.h>
#include "qemu-coroutine.h"

/*
 * Check that qemu_in_coroutine() works
 */

static void coroutine_fn verify_in_coroutine(void *opaque)
{
    g_assert(qemu_in_coroutine());
}

static void test_in_coroutine(void)
{
    Coroutine *coroutine;

    g_assert(!qemu_in_coroutine());

    coroutine = qemu_coroutine_create(verify_in_coroutine);
    qemu_coroutine_enter(coroutine, NULL);
}

/*
 * Check that qemu_coroutine_self() works
 */

static void coroutine_fn verify_self(void *opaque)
{
    g_assert(qemu_coroutine_self() == opaque);
}

static void test_self(void)
{
    Coroutine *coroutine;

    coroutine = qemu_coroutine_create(verify_self);
    qemu_coroutine_enter(coroutine, coroutine);
}

/*
 * Check that coroutines may nest multiple levels
 */

typedef struct {
    unsigned int n_enter;   /* num coroutines entered */
    unsigned int n_return;  /* num coroutines returned */
    unsigned int max;       /* maximum level of nesting */
} NestData;

static void coroutine_fn nest(void *opaque)
{
    NestData *nd = opaque;

    nd->n_enter++;

    if (nd->n_enter < nd->max) {
        Coroutine *child;

        child = qemu_coroutine_create(nest);
        qemu_coroutine_enter(child, nd);
    }

    nd->n_return++;
}

static void test_nesting(void)
{
    Coroutine *root;
    NestData nd = {
        .n_enter  = 0,
        .n_return = 0,
        .max      = 128,
    };

    root = qemu_coroutine_create(nest);
    qemu_coroutine_enter(root, &nd);

    /* Must enter and return from max nesting level */
    g_assert_cmpint(nd.n_enter, ==, nd.max);
    g_assert_cmpint(nd.n_return, ==, nd.max);
}

/*
 * Check that yield/enter transfer control correctly
 */

static void coroutine_fn yield_5_times(void *opaque)
{
    bool *done = opaque;
    int i;

    for (i = 0; i < 5; i++) {
        qemu_coroutine_yield();
    }
    *done = true;
}

static void test_yield(void)
{
    Coroutine *coroutine;
    bool done = false;
    int i = -1; /* one extra time to return from coroutine */

    coroutine = qemu_coroutine_create(yield_5_times);
    while (!done) {
        qemu_coroutine_enter(coroutine, &done);
        i++;
    }
    g_assert_cmpint(i, ==, 5); /* coroutine must yield 5 times */
}

/*
 * Check that creation, enter, and return work
 */

static void coroutine_fn set_and_exit(void *opaque)
{
    bool *done = opaque;

    *done = true;
}

static void test_lifecycle(void)
{
    Coroutine *coroutine;
    bool done = false;

    /* Create, enter, and return from coroutine */
    coroutine = qemu_coroutine_create(set_and_exit);
    qemu_coroutine_enter(coroutine, &done);
    g_assert(done); /* expect done to be true (first time) */

    /* Repeat to check that no state affects this test */
    done = false;
    coroutine = qemu_coroutine_create(set_and_exit);
    qemu_coroutine_enter(coroutine, &done);
    g_assert(done); /* expect done to be true (second time) */
}

/*
 * Lifecycle benchmark
 */

static void coroutine_fn empty_coroutine(void *opaque)
{
    /* Do nothing */
}

static void perf_lifecycle(void)
{
    Coroutine *coroutine;
    unsigned int i, max;
    double duration;

    max = 1000000;

    g_test_timer_start();
    for (i = 0; i < max; i++) {
        coroutine = qemu_coroutine_create(empty_coroutine);
        qemu_coroutine_enter(coroutine, NULL);
    }
    duration = g_test_timer_elapsed();

    g_test_message("Lifecycle %u iterations: %f s\n", max, duration);
}

static void perf_nesting(void)
{
    unsigned int i, maxcycles, maxnesting;
    double duration;

    maxcycles = 100000000;
    maxnesting = 20000;
    Coroutine *root;
    NestData nd = {
        .n_enter  = 0,
        .n_return = 0,
        .max      = maxnesting,
    };

    g_test_timer_start();
    for (i = 0; i < maxcycles; i++) {
        root = qemu_coroutine_create(nest);
        qemu_coroutine_enter(root, &nd);
    }
    duration = g_test_timer_elapsed();

    g_test_message("Nesting %u iterations of %u depth each: %f s\n",
        maxcycles, maxnesting, duration);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/basic/lifecycle", test_lifecycle);
    g_test_add_func("/basic/yield", test_yield);
    g_test_add_func("/basic/nesting", test_nesting);
    g_test_add_func("/basic/self", test_self);
    g_test_add_func("/basic/in_coroutine", test_in_coroutine);
    if (g_test_perf()) {
        g_test_add_func("/perf/lifecycle", perf_lifecycle);
        g_test_add_func("/perf/nesting", perf_nesting);
    }
    return g_test_run();
}
