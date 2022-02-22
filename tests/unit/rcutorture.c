/*
 * rcutorture.c: simple user-level performance/stress test of RCU.
 *
 * Usage:
 *     ./rcu <nreaders> rperf [ <seconds> ]
 *         Run a read-side performance test with the specified
 *         number of readers for <seconds> seconds.
 *     ./rcu <nupdaters> uperf [ <seconds> ]
 *         Run an update-side performance test with the specified
 *         number of updaters and specified duration.
 *     ./rcu <nreaders> perf [ <seconds> ]
 *         Run a combined read/update performance test with the specified
 *         number of readers and one updater and specified duration.
 *
 * The above tests produce output as follows:
 *
 * n_reads: 46008000  n_updates: 146026  nreaders: 2  nupdaters: 1 duration: 1
 * ns/read: 43.4707  ns/update: 6848.1
 *
 * The first line lists the total number of RCU reads and updates executed
 * during the test, the number of reader threads, the number of updater
 * threads, and the duration of the test in seconds.  The second line
 * lists the average duration of each type of operation in nanoseconds,
 * or "nan" if the corresponding type of operation was not performed.
 *
 *     ./rcu <nreaders> stress [ <seconds> ]
 *         Run a stress test with the specified number of readers and
 *         one updater.
 *
 * This test produces output as follows:
 *
 * n_reads: 114633217  n_updates: 3903415  n_mberror: 0
 * rcu_stress_count: 114618391 14826 0 0 0 0 0 0 0 0 0
 *
 * The first line lists the number of RCU read and update operations
 * executed, followed by the number of memory-ordering violations
 * (which will be zero in a correct RCU implementation).  The second
 * line lists the number of readers observing progressively more stale
 * data.  A correct RCU implementation will have all but the first two
 * numbers non-zero.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (c) 2008 Paul E. McKenney, IBM Corporation.
 */

/*
 * Test variables.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"

int nthreadsrunning;

#define GOFLAG_INIT 0
#define GOFLAG_RUN  1
#define GOFLAG_STOP 2

static volatile int goflag = GOFLAG_INIT;

#define RCU_READ_RUN 1000

#define NR_THREADS 100
static QemuThread threads[NR_THREADS];
static struct rcu_reader_data *data[NR_THREADS];
static int n_threads;

/*
 * Statistical counts
 *
 * These are the sum of local counters at the end of a run.
 * Updates are protected by a mutex.
 */
static QemuMutex counts_mutex;
long long n_reads = 0LL;
long n_updates = 0L;

static void create_thread(void *(*func)(void *))
{
    if (n_threads >= NR_THREADS) {
        fprintf(stderr, "Thread limit of %d exceeded!\n", NR_THREADS);
        exit(-1);
    }
    qemu_thread_create(&threads[n_threads], "test", func, &data[n_threads],
                       QEMU_THREAD_JOINABLE);
    n_threads++;
}

static void wait_all_threads(void)
{
    int i;

    for (i = 0; i < n_threads; i++) {
        qemu_thread_join(&threads[i]);
    }
    n_threads = 0;
}

/*
 * Performance test.
 */

static void *rcu_read_perf_test(void *arg)
{
    int i;
    long long n_reads_local = 0;

    rcu_register_thread();

    *(struct rcu_reader_data **)arg = get_ptr_rcu_reader();
    qatomic_inc(&nthreadsrunning);
    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }
    while (goflag == GOFLAG_RUN) {
        for (i = 0; i < RCU_READ_RUN; i++) {
            rcu_read_lock();
            rcu_read_unlock();
        }
        n_reads_local += RCU_READ_RUN;
    }
    qemu_mutex_lock(&counts_mutex);
    n_reads += n_reads_local;
    qemu_mutex_unlock(&counts_mutex);

    rcu_unregister_thread();
    return NULL;
}

static void *rcu_update_perf_test(void *arg)
{
    long long n_updates_local = 0;

    rcu_register_thread();

    *(struct rcu_reader_data **)arg = get_ptr_rcu_reader();
    qatomic_inc(&nthreadsrunning);
    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }
    while (goflag == GOFLAG_RUN) {
        synchronize_rcu();
        n_updates_local++;
    }
    qemu_mutex_lock(&counts_mutex);
    n_updates += n_updates_local;
    qemu_mutex_unlock(&counts_mutex);

    rcu_unregister_thread();
    return NULL;
}

static void perftestinit(void)
{
    nthreadsrunning = 0;
}

static void perftestrun(int nthreads, int duration, int nreaders, int nupdaters)
{
    while (qatomic_read(&nthreadsrunning) < nthreads) {
        g_usleep(1000);
    }
    goflag = GOFLAG_RUN;
    g_usleep(duration * G_USEC_PER_SEC);
    goflag = GOFLAG_STOP;
    wait_all_threads();
    printf("n_reads: %lld  n_updates: %ld  nreaders: %d  nupdaters: %d duration: %d\n",
           n_reads, n_updates, nreaders, nupdaters, duration);
    printf("ns/read: %g  ns/update: %g\n",
           ((duration * 1000*1000*1000.*(double)nreaders) /
        (double)n_reads),
           ((duration * 1000*1000*1000.*(double)nupdaters) /
        (double)n_updates));
    exit(0);
}

static void perftest(int nreaders, int duration)
{
    int i;

    perftestinit();
    for (i = 0; i < nreaders; i++) {
        create_thread(rcu_read_perf_test);
    }
    create_thread(rcu_update_perf_test);
    perftestrun(i + 1, duration, nreaders, 1);
}

static void rperftest(int nreaders, int duration)
{
    int i;

    perftestinit();
    for (i = 0; i < nreaders; i++) {
        create_thread(rcu_read_perf_test);
    }
    perftestrun(i, duration, nreaders, 0);
}

static void uperftest(int nupdaters, int duration)
{
    int i;

    perftestinit();
    for (i = 0; i < nupdaters; i++) {
        create_thread(rcu_update_perf_test);
    }
    perftestrun(i, duration, 0, nupdaters);
}

/*
 * Stress test.
 */

#define RCU_STRESS_PIPE_LEN 10

struct rcu_stress {
    int age;  /* how many update cycles while not rcu_stress_current */
    int mbtest;
};

struct rcu_stress rcu_stress_array[RCU_STRESS_PIPE_LEN] = { { 0 } };
struct rcu_stress *rcu_stress_current;
int n_mberror;

/* Updates protected by counts_mutex */
long long rcu_stress_count[RCU_STRESS_PIPE_LEN + 1];


static void *rcu_read_stress_test(void *arg)
{
    int i;
    struct rcu_stress *p;
    int pc;
    long long n_reads_local = 0;
    long long rcu_stress_local[RCU_STRESS_PIPE_LEN + 1] = { 0 };
    volatile int garbage = 0;

    rcu_register_thread();

    *(struct rcu_reader_data **)arg = get_ptr_rcu_reader();
    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }
    while (goflag == GOFLAG_RUN) {
        rcu_read_lock();
        p = qatomic_rcu_read(&rcu_stress_current);
        if (qatomic_read(&p->mbtest) == 0) {
            n_mberror++;
        }
        rcu_read_lock();
        for (i = 0; i < 100; i++) {
            garbage++;
        }
        rcu_read_unlock();
        pc = qatomic_read(&p->age);
        rcu_read_unlock();
        if ((pc > RCU_STRESS_PIPE_LEN) || (pc < 0)) {
            pc = RCU_STRESS_PIPE_LEN;
        }
        rcu_stress_local[pc]++;
        n_reads_local++;
    }
    qemu_mutex_lock(&counts_mutex);
    n_reads += n_reads_local;
    for (i = 0; i <= RCU_STRESS_PIPE_LEN; i++) {
        rcu_stress_count[i] += rcu_stress_local[i];
    }
    qemu_mutex_unlock(&counts_mutex);

    rcu_unregister_thread();
    return NULL;
}

/*
 * Stress Test Updater
 *
 * The updater cycles around updating rcu_stress_current to point at
 * one of the rcu_stress_array_entries and resets it's age. It
 * then increments the age of all the other entries. The age
 * will be read under an rcu_read_lock() and distribution of values
 * calculated. The final result gives an indication of how many
 * previously current rcu_stress entries are in flight until the RCU
 * cycle complete.
 */
static void *rcu_update_stress_test(void *arg)
{
    int i, rcu_stress_idx = 0;
    struct rcu_stress *cp = qatomic_read(&rcu_stress_current);

    rcu_register_thread();
    *(struct rcu_reader_data **)arg = get_ptr_rcu_reader();

    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }

    while (goflag == GOFLAG_RUN) {
        struct rcu_stress *p;
        rcu_stress_idx++;
        if (rcu_stress_idx >= RCU_STRESS_PIPE_LEN) {
            rcu_stress_idx = 0;
        }
        p = &rcu_stress_array[rcu_stress_idx];
        /* catching up with ourselves would be a bug */
        assert(p != cp);
        qatomic_set(&p->mbtest, 0);
        smp_mb();
        qatomic_set(&p->age, 0);
        qatomic_set(&p->mbtest, 1);
        qatomic_rcu_set(&rcu_stress_current, p);
        cp = p;
        /*
         * New RCU structure is now live, update pipe counts on old
         * ones.
         */
        for (i = 0; i < RCU_STRESS_PIPE_LEN; i++) {
            if (i != rcu_stress_idx) {
                qatomic_set(&rcu_stress_array[i].age,
                           rcu_stress_array[i].age + 1);
            }
        }
        synchronize_rcu();
        n_updates++;
    }

    rcu_unregister_thread();
    return NULL;
}

static void *rcu_fake_update_stress_test(void *arg)
{
    rcu_register_thread();

    *(struct rcu_reader_data **)arg = get_ptr_rcu_reader();
    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }
    while (goflag == GOFLAG_RUN) {
        synchronize_rcu();
        g_usleep(1000);
    }

    rcu_unregister_thread();
    return NULL;
}

static void stresstest(int nreaders, int duration)
{
    int i;

    rcu_stress_current = &rcu_stress_array[0];
    rcu_stress_current->age = 0;
    rcu_stress_current->mbtest = 1;
    for (i = 0; i < nreaders; i++) {
        create_thread(rcu_read_stress_test);
    }
    create_thread(rcu_update_stress_test);
    for (i = 0; i < 5; i++) {
        create_thread(rcu_fake_update_stress_test);
    }
    goflag = GOFLAG_RUN;
    g_usleep(duration * G_USEC_PER_SEC);
    goflag = GOFLAG_STOP;
    wait_all_threads();
    printf("n_reads: %lld  n_updates: %ld  n_mberror: %d\n",
           n_reads, n_updates, n_mberror);
    printf("rcu_stress_count:");
    for (i = 0; i <= RCU_STRESS_PIPE_LEN; i++) {
        printf(" %lld", rcu_stress_count[i]);
    }
    printf("\n");
    exit(0);
}

/* GTest interface */

static void gtest_stress(int nreaders, int duration)
{
    int i;

    rcu_stress_current = &rcu_stress_array[0];
    rcu_stress_current->age = 0;
    rcu_stress_current->mbtest = 1;
    for (i = 0; i < nreaders; i++) {
        create_thread(rcu_read_stress_test);
    }
    create_thread(rcu_update_stress_test);
    for (i = 0; i < 5; i++) {
        create_thread(rcu_fake_update_stress_test);
    }
    goflag = GOFLAG_RUN;
    g_usleep(duration * G_USEC_PER_SEC);
    goflag = GOFLAG_STOP;
    wait_all_threads();
    g_assert_cmpint(n_mberror, ==, 0);
    for (i = 2; i <= RCU_STRESS_PIPE_LEN; i++) {
        g_assert_cmpint(rcu_stress_count[i], ==, 0);
    }
}

static void gtest_stress_1_1(void)
{
    gtest_stress(1, 1);
}

static void gtest_stress_10_1(void)
{
    gtest_stress(10, 1);
}

static void gtest_stress_1_5(void)
{
    gtest_stress(1, 5);
}

static void gtest_stress_10_5(void)
{
    gtest_stress(10, 5);
}

/*
 * Mainprogram.
 */

static void usage(int argc, char *argv[])
{
    fprintf(stderr, "Usage: %s [nreaders [ [r|u]perf | stress [duration]]\n",
            argv[0]);
    exit(-1);
}

int main(int argc, char *argv[])
{
    int nreaders = 1;
    int duration = 1;

    qemu_mutex_init(&counts_mutex);
    if (argc >= 2 && argv[1][0] == '-') {
        g_test_init(&argc, &argv, NULL);
        if (g_test_quick()) {
            g_test_add_func("/rcu/torture/1reader", gtest_stress_1_1);
            g_test_add_func("/rcu/torture/10readers", gtest_stress_10_1);
        } else {
            g_test_add_func("/rcu/torture/1reader", gtest_stress_1_5);
            g_test_add_func("/rcu/torture/10readers", gtest_stress_10_5);
        }
        return g_test_run();
    }

    if (argc >= 2) {
        nreaders = strtoul(argv[1], NULL, 0);
    }
    if (argc > 3) {
        duration = strtoul(argv[3], NULL, 0);
    }
    if (argc < 3 || strcmp(argv[2], "stress") == 0) {
        stresstest(nreaders, duration);
    } else if (strcmp(argv[2], "rperf") == 0) {
        rperftest(nreaders, duration);
    } else if (strcmp(argv[2], "uperf") == 0) {
        uperftest(nreaders, duration);
    } else if (strcmp(argv[2], "perf") == 0) {
        perftest(nreaders, duration);
    }
    usage(argc, argv);
    return 0;
}
