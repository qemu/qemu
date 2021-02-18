/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

/* Using volatile because we are testing atomics */
static inline int atomic_inc32(volatile int *x)
{
    int old, dummy;
    __asm__ __volatile__(
        "1: %0 = memw_locked(%2)\n\t"
        "   %1 = add(%0, #1)\n\t"
        "   memw_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

/* Using volatile because we are testing atomics */
static inline long long atomic_inc64(volatile long long *x)
{
    long long old, dummy;
    __asm__ __volatile__(
        "1: %0 = memd_locked(%2)\n\t"
        "   %1 = #1\n\t"
        "   %1 = add(%0, %1)\n\t"
        "   memd_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

/* Using volatile because we are testing atomics */
static inline int atomic_dec32(volatile int *x)
{
    int old, dummy;
    __asm__ __volatile__(
        "1: %0 = memw_locked(%2)\n\t"
        "   %1 = add(%0, #-1)\n\t"
        "   memw_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

/* Using volatile because we are testing atomics */
static inline long long atomic_dec64(volatile long long *x)
{
    long long old, dummy;
    __asm__ __volatile__(
        "1: %0 = memd_locked(%2)\n\t"
        "   %1 = #-1\n\t"
        "   %1 = add(%0, %1)\n\t"
        "   memd_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

#define LOOP_CNT 1000
/* Using volatile because we are testing atomics */
volatile int tick32 = 1;
/* Using volatile because we are testing atomics */
volatile long long tick64 = 1;
int err;

void *thread1_func(void *arg)
{
    int i;

    for (i = 0; i < LOOP_CNT; i++) {
        atomic_inc32(&tick32);
        atomic_dec64(&tick64);
    }
    return NULL;
}

void *thread2_func(void *arg)
{
    int i;
    for (i = 0; i < LOOP_CNT; i++) {
        atomic_dec32(&tick32);
        atomic_inc64(&tick64);
    }
    return NULL;
}

void test_pthread(void)
{
    pthread_t tid1, tid2;

    pthread_create(&tid1, NULL, thread1_func, "hello1");
    pthread_create(&tid2, NULL, thread2_func, "hello2");
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    if (tick32 != 1) {
        printf("ERROR: tick32 %d != 1\n", tick32);
        err++;
    }
    if (tick64 != 1) {
        printf("ERROR: tick64 %lld != 1\n", tick64);
        err++;
    }
}

int main(int argc, char **argv)
{
    test_pthread();
    puts(err ? "FAIL" : "PASS");
    return err;
}
