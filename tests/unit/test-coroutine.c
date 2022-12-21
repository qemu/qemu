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

static void test_no_dangling_access(void)
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

static bool locked;
static int done;

static void coroutine_fn mutex_fn(void *opaque)
{
    CoMutex *m = opaque;
    qemu_co_mutex_lock(m);
    assert(!locked);
    locked = true;
    qemu_coroutine_yield();
    locked = false;
    qemu_co_mutex_unlock(m);
    done++;
}

static void coroutine_fn lockable_fn(void *opaque)
{
    QemuLockable *x = opaque;
    qemu_lockable_lock(x);
    assert(!locked);
    locked = true;
    qemu_coroutine_yield();
    locked = false;
    qemu_lockable_unlock(x);
    done++;
}

static void do_test_co_mutex(CoroutineEntry *entry, void *opaque)
{
    Coroutine *c1 = qemu_coroutine_create(entry, opaque);
    Coroutine *c2 = qemu_coroutine_create(entry, opaque);

    done = 0;
    qemu_coroutine_enter(c1);
    g_assert(locked);
    qemu_coroutine_enter(c2);

    /* Unlock queues c2.  It is then started automatically when c1 yields or
     * terminates.
     */
    qemu_coroutine_enter(c1);
    g_assert_cmpint(done, ==, 1);
    g_assert(locked);

    qemu_coroutine_enter(c2);
    g_assert_cmpint(done, ==, 2);
    g_assert(!locked);
}

static void test_co_mutex(void)
{
    CoMutex m;

    qemu_co_mutex_init(&m);
    do_test_co_mutex(mutex_fn, &m);
}

static void test_co_mutex_lockable(void)
{
    CoMutex m;
    CoMutex *null_pointer = NULL;

    qemu_co_mutex_init(&m);
    do_test_co_mutex(lockable_fn, QEMU_MAKE_LOCKABLE(&m));

    g_assert(QEMU_MAKE_LOCKABLE(null_pointer) == NULL);
}

static CoRwlock rwlock;

/* Test that readers are properly sent back to the queue when upgrading,
 * even if they are the sole readers.  The test scenario is as follows:
 *
 *
 * | c1           | c2         |
 * |--------------+------------+
 * | rdlock       |            |
 * | yield        |            |
 * |              | wrlock     |
 * |              | <queued>   |
 * | upgrade      |            |
 * | <queued>     | <dequeued> |
 * |              | unlock     |
 * | <dequeued>   |            |
 * | unlock       |            |
 */

static void coroutine_fn rwlock_yield_upgrade(void *opaque)
{
    qemu_co_rwlock_rdlock(&rwlock);
    qemu_coroutine_yield();

    qemu_co_rwlock_upgrade(&rwlock);
    qemu_co_rwlock_unlock(&rwlock);

    *(bool *)opaque = true;
}

static void coroutine_fn rwlock_wrlock_yield(void *opaque)
{
    qemu_co_rwlock_wrlock(&rwlock);
    qemu_coroutine_yield();

    qemu_co_rwlock_unlock(&rwlock);
    *(bool *)opaque = true;
}

static void test_co_rwlock_upgrade(void)
{
    bool c1_done = false;
    bool c2_done = false;
    Coroutine *c1, *c2;

    qemu_co_rwlock_init(&rwlock);
    c1 = qemu_coroutine_create(rwlock_yield_upgrade, &c1_done);
    c2 = qemu_coroutine_create(rwlock_wrlock_yield, &c2_done);

    qemu_coroutine_enter(c1);
    qemu_coroutine_enter(c2);

    /* c1 now should go to sleep.  */
    qemu_coroutine_enter(c1);
    g_assert(!c1_done);

    qemu_coroutine_enter(c2);
    g_assert(c1_done);
    g_assert(c2_done);
}

static void coroutine_fn rwlock_rdlock_yield(void *opaque)
{
    qemu_co_rwlock_rdlock(&rwlock);
    qemu_coroutine_yield();

    qemu_co_rwlock_unlock(&rwlock);
    qemu_coroutine_yield();

    *(bool *)opaque = true;
}

static void coroutine_fn rwlock_wrlock_downgrade(void *opaque)
{
    qemu_co_rwlock_wrlock(&rwlock);

    qemu_co_rwlock_downgrade(&rwlock);
    qemu_co_rwlock_unlock(&rwlock);
    *(bool *)opaque = true;
}

static void coroutine_fn rwlock_rdlock(void *opaque)
{
    qemu_co_rwlock_rdlock(&rwlock);

    qemu_co_rwlock_unlock(&rwlock);
    *(bool *)opaque = true;
}

static void coroutine_fn rwlock_wrlock(void *opaque)
{
    qemu_co_rwlock_wrlock(&rwlock);

    qemu_co_rwlock_unlock(&rwlock);
    *(bool *)opaque = true;
}

/*
 * Check that downgrading a reader-writer lock does not cause a hang.
 *
 * Four coroutines are used to produce a situation where there are
 * both reader and writer hopefuls waiting to acquire an rwlock that
 * is held by a reader.
 *
 * The correct sequence of operations we aim to provoke can be
 * represented as:
 *
 * | c1     | c2         | c3         | c4         |
 * |--------+------------+------------+------------|
 * | rdlock |            |            |            |
 * | yield  |            |            |            |
 * |        | wrlock     |            |            |
 * |        | <queued>   |            |            |
 * |        |            | rdlock     |            |
 * |        |            | <queued>   |            |
 * |        |            |            | wrlock     |
 * |        |            |            | <queued>   |
 * | unlock |            |            |            |
 * | yield  |            |            |            |
 * |        | <dequeued> |            |            |
 * |        | downgrade  |            |            |
 * |        |            | <dequeued> |            |
 * |        |            | unlock     |            |
 * |        | ...        |            |            |
 * |        | unlock     |            |            |
 * |        |            |            | <dequeued> |
 * |        |            |            | unlock     |
 */
static void test_co_rwlock_downgrade(void)
{
    bool c1_done = false;
    bool c2_done = false;
    bool c3_done = false;
    bool c4_done = false;
    Coroutine *c1, *c2, *c3, *c4;

    qemu_co_rwlock_init(&rwlock);

    c1 = qemu_coroutine_create(rwlock_rdlock_yield, &c1_done);
    c2 = qemu_coroutine_create(rwlock_wrlock_downgrade, &c2_done);
    c3 = qemu_coroutine_create(rwlock_rdlock, &c3_done);
    c4 = qemu_coroutine_create(rwlock_wrlock, &c4_done);

    qemu_coroutine_enter(c1);
    qemu_coroutine_enter(c2);
    qemu_coroutine_enter(c3);
    qemu_coroutine_enter(c4);

    qemu_coroutine_enter(c1);

    g_assert(c2_done);
    g_assert(c3_done);
    g_assert(c4_done);

    qemu_coroutine_enter(c1);

    g_assert(c1_done);
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

    g_test_message("Lifecycle %u iterations: %f s", max, duration);
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

    g_test_message("Nesting %u iterations of %u depth each: %f s",
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

    g_test_message("Yield %u iterations: %f s", maxcycles, duration);
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

    g_test_message("Function call %u iterations: %f s", maxcycles, duration);
}

static __attribute__((noinline)) void coroutine_fn perf_cost_func(void *opaque)
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
        g_test_add_func("/basic/no-dangling-access", test_no_dangling_access);
    }

    g_test_add_func("/basic/lifecycle", test_lifecycle);
    g_test_add_func("/basic/yield", test_yield);
    g_test_add_func("/basic/nesting", test_nesting);
    g_test_add_func("/basic/self", test_self);
    g_test_add_func("/basic/entered", test_entered);
    g_test_add_func("/basic/in_coroutine", test_in_coroutine);
    g_test_add_func("/basic/order", test_order);
    g_test_add_func("/locking/co-mutex", test_co_mutex);
    g_test_add_func("/locking/co-mutex/lockable", test_co_mutex_lockable);
    g_test_add_func("/locking/co-rwlock/upgrade", test_co_rwlock_upgrade);
    g_test_add_func("/locking/co-rwlock/downgrade", test_co_rwlock_downgrade);
    if (g_test_perf()) {
        g_test_add_func("/perf/lifecycle", perf_lifecycle);
        g_test_add_func("/perf/nesting", perf_nesting);
        g_test_add_func("/perf/yield", perf_yield);
        g_test_add_func("/perf/function-call", perf_baseline);
        g_test_add_func("/perf/cost", perf_cost);
    }
    return g_test_run();
}
