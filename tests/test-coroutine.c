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

#include "qemu/osdep.h"
#include "qemu/coroutine.h"
#include "qemu/coroutine_int.h"

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

    coroutine = qemu_coroutine_create(verify_in_coroutine, NULL);
    qemu_coroutine_enter(coroutine);
}

/*
 * Check that qemu_coroutine_self() works
 */

static void coroutine_fn verify_self(void *opaque)
{
    Coroutine **p_co = opaque;
    g_assert(qemu_coroutine_self() == *p_co);
}

static void test_self(void)
{
    Coroutine *coroutine;

    coroutine = qemu_coroutine_create(verify_self, &coroutine);
    qemu_coroutine_enter(coroutine);
}

/*
 * Check that qemu_coroutine_entered() works
 */

static void coroutine_fn verify_entered_step_2(void *opaque)
{
    Coroutine *caller = (Coroutine *)opaque;

    g_assert(qemu_coroutine_entered(caller));
    g_assert(qemu_coroutine_entered(qemu_coroutine_self()));
    qemu_coroutine_yield();

    /* Once more to check it still works after yielding */
    g_assert(qemu_coroutine_entered(caller));
    g_assert(qemu_coroutine_entered(qemu_coroutine_self()));
    qemu_coroutine_yield();
}

static void coroutine_fn verify_entered_step_1(void *opaque)
{
    Coroutine *self = qemu_coroutine_self();
    Coroutine *coroutine;

    g_assert(qemu_coroutine_entered(self));

    coroutine = qemu_coroutine_create(verify_entered_step_2, self);
    g_assert(!qemu_coroutine_entered(coroutine));
    qemu_coroutine_enter(coroutine);
    g_assert(!qemu_coroutine_entered(coroutine));
    qemu_coroutine_enter(coroutine);
}

static void test_entered(void)
{
    Coroutine *coroutine;

    coroutine = qemu_coroutine_create(verify_entered_step_1, NULL);
    g_assert(!qemu_coroutine_entered(coroutine));
    qemu_coroutine_enter(coroutine);
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

        child = qemu_coroutine_create(nest, nd);
        qemu_coroutine_enter(child);
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

    root = qemu_coroutine_create(nest, &nd);
    qemu_coroutine_enter(root);

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

    coroutine = qemu_coroutine_create(yield_5_times, &done);
    while (!done) {
        qemu_coroutine_enter(coroutine);
        i++;
    }
    g_assert_cmpint(i, ==, 5); /* coroutine must yield 5 times */
}

static void coroutine_fn c2_fn(void *opaque)
{
    qemu_coroutine_yield();
}

static void coroutine_fn c1_fn(void *opaque)
{
    Coroutine *c2 = opaque;
    qemu_coroutine_enter(c2);
}

static void test_co_queue(void)
{
    Coroutine *c1;
    Coroutine *c2;
    Coroutine tmp;

    c2 = qemu_coroutine_create(c2_fn, NULL);
    c1 = qemu_coroutine_create(c1_fn, c2);

    qemu_coroutine_enter(c1);

    /* c1 shouldn't be used any more now; make sure we segfault if it is */
    tmp = *c1;
    memset(c1, 0xff, sizeof(Coroutine));
    qemu_coroutine_enter(c2);

    /* Must restore the coroutine now to avoid corrupted pool */
    *c1 = tmp;
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
    coroutine = qemu_coroutine_create(set_and_exit, &done);
    qemu_coroutine_enter(coroutine);
    g_assert(done); /* expect done to be true (first time) */

    /* Repeat to check that no state affects this test */
    done = false;
    coroutine = qemu_coroutine_create(set_and_exit, &done);
    qemu_coroutine_enter(coroutine);
    g_assert(done); /* expect done to be true (second time) */
}


#define RECORD_SIZE 10 /* Leave some room for expansion */
struct coroutine_position {
    int func;
    int state;
};
static struct coroutine_position records[RECORD_SIZE];
static unsigned record_pos;

static void record_push(int func, int state)
{
    struct coroutine_position *cp = &records[record_pos++];
    g_assert_cmpint(record_pos, <, RECORD_SIZE);
    cp->func = func;
    cp->state = state;
}

static void coroutine_fn co_order_test(void *opaque)
{
    record_push(2, 1);
    g_assert(qemu_in_coroutine());
    qemu_coroutine_yield();
    record_push(2, 2);
    g_assert(qemu_in_coroutine());
}

static void do_order_test(void)
{
    Coroutine *co;

    co = qemu_coroutine_create(co_order_test, NULL);
    record_push(1, 1);
    qemu_coroutine_enter(co);
    record_push(1, 2);
    g_assert(!qemu_in_coroutine());
    qemu_coroutine_enter(co);
    record_push(1, 3);
    g_assert(!qemu_in_coroutine());
}

static void test_order(void)
{
    int i;
    const struct coroutine_position expected_pos[] = {
        {1, 1,}, {2, 1}, {1, 2}, {2, 2}, {1, 3}
    };
    do_order_test();
    g_assert_cmpint(record_pos, ==, 5);
    for (i = 0; i < record_pos; i++) {
        g_assert_cmpint(records[i].func , ==, expected_pos[i].func );
        g_assert_cmpint(records[i].state, ==, expected_pos[i].state);
    }
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
        coroutine = qemu_coroutine_create(empty_coroutine, NULL);
        qemu_coroutine_enter(coroutine);
    }
    duration = g_test_timer_elapsed();

    g_test_message("Lifecycle %u iterations: %f s\n", max, duration);
}

static void perf_nesting(void)
{
    unsigned int i, maxcycles, maxnesting;
    double duration;

    maxcycles = 10000;
    maxnesting = 1000;
    Coroutine *root;

    g_test_timer_start();
    for (i = 0; i < maxcycles; i++) {
        NestData nd = {
            .n_enter  = 0,
            .n_return = 0,
            .max      = maxnesting,
        };
        root = qemu_coroutine_create(nest, &nd);
        qemu_coroutine_enter(root);
    }
    duration = g_test_timer_elapsed();

    g_test_message("Nesting %u iterations of %u depth each: %f s\n",
        maxcycles, maxnesting, duration);
}

/*
 * Yield benchmark
 */

static void coroutine_fn yield_loop(void *opaque)
{
    unsigned int *counter = opaque;

    while ((*counter) > 0) {
        (*counter)--;
        qemu_coroutine_yield();
    }
}

static void perf_yield(void)
{
    unsigned int i, maxcycles;
    double duration;

    maxcycles = 100000000;
    i = maxcycles;
    Coroutine *coroutine = qemu_coroutine_create(yield_loop, &i);

    g_test_timer_start();
    while (i > 0) {
        qemu_coroutine_enter(coroutine);
    }
    duration = g_test_timer_elapsed();

    g_test_message("Yield %u iterations: %f s\n",
        maxcycles, duration);
}

static __attribute__((noinline)) void dummy(unsigned *i)
{
    (*i)--;
}

static void perf_baseline(void)
{
    unsigned int i, maxcycles;
    double duration;

    maxcycles = 100000000;
    i = maxcycles;

    g_test_timer_start();
    while (i > 0) {
        dummy(&i);
    }
    duration = g_test_timer_elapsed();

    g_test_message("Function call %u iterations: %f s\n",
        maxcycles, duration);
}

static __attribute__((noinline)) void perf_cost_func(void *opaque)
{
    qemu_coroutine_yield();
}

static void perf_cost(void)
{
    const unsigned long maxcycles = 40000000;
    unsigned long i = 0;
    double duration;
    unsigned long ops;
    Coroutine *co;

    g_test_timer_start();
    while (i++ < maxcycles) {
        co = qemu_coroutine_create(perf_cost_func, &i);
        qemu_coroutine_enter(co);
        qemu_coroutine_enter(co);
    }
    duration = g_test_timer_elapsed();
    ops = (long)(maxcycles / (duration * 1000));

    g_test_message("Run operation %lu iterations %f s, %luK operations/s, "
                   "%luns per coroutine",
                   maxcycles,
                   duration, ops,
                   (unsigned long)(1000000000.0 * duration / maxcycles));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* This test assumes there is a freelist and marks freed coroutine memory
     * with a sentinel value.  If there is no freelist this would legitimately
     * crash, so skip it.
     */
    if (CONFIG_COROUTINE_POOL) {
        g_test_add_func("/basic/co_queue", test_co_queue);
    }

    g_test_add_func("/basic/lifecycle", test_lifecycle);
    g_test_add_func("/basic/yield", test_yield);
    g_test_add_func("/basic/nesting", test_nesting);
    g_test_add_func("/basic/self", test_self);
    g_test_add_func("/basic/entered", test_entered);
    g_test_add_func("/basic/in_coroutine", test_in_coroutine);
    g_test_add_func("/basic/order", test_order);
    if (g_test_perf()) {
        g_test_add_func("/perf/lifecycle", perf_lifecycle);
        g_test_add_func("/perf/nesting", perf_nesting);
        g_test_add_func("/perf/yield", perf_yield);
        g_test_add_func("/perf/function-call", perf_baseline);
        g_test_add_func("/perf/cost", perf_cost);
    }
    return g_test_run();
}
