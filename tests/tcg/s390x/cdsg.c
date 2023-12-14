/*
 * Test CDSG instruction.
 *
 * Increment the first half of aligned_quadword by 1, and the second half by 2
 * from 2 threads. Verify that the result is consistent.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

static volatile bool start;
typedef unsigned long aligned_quadword[2] __attribute__((__aligned__(16)));
static aligned_quadword val;
static const int n_iterations = 1000000;

static inline int cdsg(unsigned long *orig0, unsigned long *orig1,
                       unsigned long new0, unsigned long new1,
                       aligned_quadword *mem)
{
    register unsigned long r0 asm("r0");
    register unsigned long r1 asm("r1");
    register unsigned long r2 asm("r2");
    register unsigned long r3 asm("r3");
    int cc;

    r0 = *orig0;
    r1 = *orig1;
    r2 = new0;
    r3 = new1;
    asm("cdsg %[r0],%[r2],%[db2]\n"
        "ipm %[cc]"
        : [r0] "+r" (r0)
        , [r1] "+r" (r1)
        , [db2] "+m" (*mem)
        , [cc] "=r" (cc)
        : [r2] "r" (r2)
        , [r3] "r" (r3)
        : "cc");
    *orig0 = r0;
    *orig1 = r1;

    return (cc >> 28) & 3;
}

void *cdsg_loop(void *arg)
{
    unsigned long orig0, orig1, new0, new1;
    int cc;
    int i;

    while (!start) {
    }

    orig0 = val[0];
    orig1 = val[1];
    for (i = 0; i < n_iterations;) {
        new0 = orig0 + 1;
        new1 = orig1 + 2;

        cc = cdsg(&orig0, &orig1, new0, new1, &val);

        if (cc == 0) {
            orig0 = new0;
            orig1 = new1;
            i++;
        } else {
            assert(cc == 1);
        }
    }

    return NULL;
}

int main(void)
{
    pthread_t thread;
    int ret;

    ret = pthread_create(&thread, NULL, cdsg_loop, NULL);
    assert(ret == 0);
    start = true;
    cdsg_loop(NULL);
    ret = pthread_join(thread, NULL);
    assert(ret == 0);

    assert(val[0] == n_iterations * 2);
    assert(val[1] == n_iterations * 4);

    return EXIT_SUCCESS;
}
