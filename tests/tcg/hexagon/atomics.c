/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

int err;

#include "hex_test.h"

static inline int32_t load_locked32(int32_t *x)
{
    int32_t value;

    __asm__ __volatile__("%[value] = memw_locked(%[addr])"
                         : [value] "=r"(value)
                         : [addr] "r"(x)
                         : "memory");
    return value;
}

static inline int store_conditional32(int32_t *x, int32_t value)
{
    int result;

    __asm__ __volatile__(
        "memw_locked(%[addr], p0) = %[value]\n\t"
        "%[result] = p0"
        : [result] "=r"(result)
        : [addr] "r"(x), [value] "r"(value)
        : "p0", "memory");
    return result != 0;
}

static inline int32_t atomic_inc32(int32_t *x)
{
    int32_t old, dummy;
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

static inline int64_t atomic_inc64(int64_t *x)
{
    int64_t old, dummy;
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

static inline int32_t atomic_dec32(int32_t *x)
{
    int32_t old, dummy;
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

static inline int64_t atomic_dec64(int64_t *x)
{
    int64_t old, dummy;
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

static uint32_t v85_atomic_inc(uint32_t *counter)
{
    uint32_t old_val;

    asm volatile("r0 = %0\n"
                 "r1 = %1\n"
                 "r2 = #1\n"
                 ".word 0x9001e220\n" /* r0 = atomw_add(r1,r2):aq */
                 "%0 = r0\n"
                 : "=r"(old_val)
                 : "r"(counter)
                 : "r0", "r1", "r2");
    return old_val;
}

static uint64_t v85_atomic_swap64(uint64_t *addr, uint64_t val)
{
    asm volatile("r1:0 = %0\n"
                 "r2 = %1\n"
                 ".word 0x90c2e0a0\n" /* r1:0 = atomd_swap(r2,r1:0):aq */
                 "%0 = r1:0\n"
                 : "+r"(val)
                 : "r"(addr)
                 : "r0", "r1", "r2");
    return val;
}

static void v85_dcfetch_multi(const void *addr)
{
    asm volatile("r0 = %0\n"
                 "r1 = #0\n"
                 ".word 0x9400c840\n" /* dcfetch_multi(r0,r1) */
                 :
                 : "r"(addr)
                 : "r0", "r1", "memory");
}

#define LOOP_CNT 1000
volatile int32_t tick32 = 1; /* Using volatile because we are testing atomics */
volatile int64_t tick64 = 1; /* Using volatile because we are testing atomics */

static void test_reservations(void)
{
    int32_t value = 1;

    /* A store-conditional with no preceding load-locked must fail. */
    check32(store_conditional32(&value, 2), 0);
    check32(value, 1);

    /* Returning from a syscall drops the reservation. */
    check32(load_locked32(&value), 1);
    getpid();
    check32(store_conditional32(&value, 3), 0);
    check32(value, 1);
}

void *thread1_func(void *arg)
{
    for (int i = 0; i < LOOP_CNT; i++) {
        atomic_inc32(&tick32);
        atomic_dec64(&tick64);
    }
    return NULL;
}

void *thread2_func(void *arg)
{
    for (int i = 0; i < LOOP_CNT; i++) {
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

    check32(tick32, 1);
    check64(tick64, 1);
}

static void test_v85_swap64(void)
{
#define INITIAL_V1 0xffffffffffffffff
#define INITIAL_V2 0x1111111111111111

    uint64_t v1 = INITIAL_V1;
    uint64_t v2 = INITIAL_V2;

    v2 = v85_atomic_swap64(&v1, v2);
    check64(v1, INITIAL_V2);
    check64(v2, INITIAL_V1);

    v2 = v85_atomic_swap64(&v1, v2);
    check64(v1, INITIAL_V1);
    check64(v2, INITIAL_V2);

#undef INITIAL_V1
#undef INITIAL_V2
}

#define V85_ROUNDS 1000
#define V85_THREADS 10
static uint32_t v85_global_counter = 1;
static uint32_t v85_started_threads;

static void *v85_counter_thread(void *arg)
{
    uint32_t previous;

    v85_atomic_inc(&v85_started_threads);
    while (v85_started_threads != V85_THREADS) {
        asm volatile("pause(#0)");
    }
    previous = 0;
    for (int i = 0; i < V85_ROUNDS; i++) {
        uint32_t current = v85_atomic_inc(&v85_global_counter);

        if (current <= previous) {
            printf("ERROR: counter is not strictly growing. "
                   "Before: %" PRIu32 ", Now: %" PRIu32 "\n",
                   previous, current);
            err = 1;
            return arg;
        }
        previous = current;
    }
    return arg;
}

static void test_v85_race(void)
{
    pthread_t tids[V85_THREADS];

    for (int i = 0; i < V85_THREADS; i++) {
        pthread_create(&tids[i], NULL, v85_counter_thread, NULL);
    }
    for (int i = 0; i < V85_THREADS; i++) {
        pthread_join(tids[i], NULL);
    }
    check32(v85_global_counter, 1 + V85_THREADS * V85_ROUNDS);
}

static void test_v85_inc32(void)
{
    uint32_t counter = UINT32_MAX;

    check32(v85_atomic_inc(&counter), UINT32_MAX);
    check32(counter, 0);
}

int main(int argc, char **argv)
{
    test_reservations();
    test_pthread();
    v85_dcfetch_multi(&v85_global_counter);
    test_v85_swap64();
    test_v85_race();
    test_v85_inc32();
    puts(err ? "FAIL" : "PASS");
    return err;
}
