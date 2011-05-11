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

#include <stdlib.h>
#include <stdio.h>
#include "qemu-coroutine.h"

static const char *cur_test_name;

static void test_assert(bool condition, const char *msg)
{
    if (!condition) {
        fprintf(stderr, "%s: %s\n", cur_test_name, msg);
        exit(EXIT_FAILURE);
    }
}

/*
 * Check that qemu_in_coroutine() works
 */

static void coroutine_fn verify_in_coroutine(void *opaque)
{
    test_assert(qemu_in_coroutine(), "expected coroutine context");
}

static void test_in_coroutine(void)
{
    Coroutine *coroutine;

    test_assert(!qemu_in_coroutine(), "expected no coroutine context");

    coroutine = qemu_coroutine_create(verify_in_coroutine);
    qemu_coroutine_enter(coroutine, NULL);
}

/*
 * Check that qemu_coroutine_self() works
 */

static void coroutine_fn verify_self(void *opaque)
{
    test_assert(qemu_coroutine_self() == opaque,
                "qemu_coroutine_self() did not return this coroutine");
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
        .max      = 1,
    };

    root = qemu_coroutine_create(nest);
    qemu_coroutine_enter(root, &nd);

    test_assert(nd.n_enter == nd.max,
                "failed entering to max nesting level");
    test_assert(nd.n_return == nd.max,
                "failed returning from max nesting level");
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
    test_assert(i == 5, "coroutine did not yield 5 times");
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
    test_assert(done, "expected done to be true (first time)");

    /* Repeat to check that no state affects this test */
    done = false;
    coroutine = qemu_coroutine_create(set_and_exit);
    qemu_coroutine_enter(coroutine, &done);
    test_assert(done, "expected done to be true (second time)");
}

#define TESTCASE(fn) { #fn, fn }
int main(int argc, char **argv)
{
    static struct {
        const char *name;
        void (*run)(void);
    } testcases[] = {
        TESTCASE(test_lifecycle),
        TESTCASE(test_yield),
        TESTCASE(test_nesting),
        TESTCASE(test_self),
        TESTCASE(test_in_coroutine),
        {},
    };
    int i;

    for (i = 0; testcases[i].name; i++) {
        cur_test_name = testcases[i].name;
        printf("%s\n", testcases[i].name);
        testcases[i].run();
    }
    return EXIT_SUCCESS;
}
