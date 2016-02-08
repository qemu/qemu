/*
 * rcuq_test.c
 *
 * usage: rcuq_test <readers> <duration>
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
 * Copyright (c) 2013 Mike D. Day, IBM Corporation.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "qemu/atomic.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "qemu/rcu_queue.h"

/*
 * Test variables.
 */

static QemuMutex counts_mutex;
static long long n_reads = 0LL;
static long long n_updates = 0LL;
static long long n_reclaims = 0LL;
static long long n_nodes_removed = 0LL;
static long long n_nodes = 0LL;
static int g_test_in_charge = 0;

static int nthreadsrunning;

#define GOFLAG_INIT 0
#define GOFLAG_RUN  1
#define GOFLAG_STOP 2

static volatile int goflag = GOFLAG_INIT;

#define RCU_READ_RUN 1000
#define RCU_UPDATE_RUN 10
#define NR_THREADS 100
#define RCU_Q_LEN 100

static QemuThread threads[NR_THREADS];
static struct rcu_reader_data *data[NR_THREADS];
static int n_threads;

static int select_random_el(int max)
{
    return (rand() % max);
}


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


struct list_element {
    QLIST_ENTRY(list_element) entry;
    struct rcu_head rcu;
};

static void reclaim_list_el(struct rcu_head *prcu)
{
    struct list_element *el = container_of(prcu, struct list_element, rcu);
    g_free(el);
    /* Accessed only from call_rcu thread.  */
    n_reclaims++;
}

static QLIST_HEAD(q_list_head, list_element) Q_list_head;

static void *rcu_q_reader(void *arg)
{
    long long n_reads_local = 0;
    struct list_element *el;

    rcu_register_thread();

    *(struct rcu_reader_data **)arg = &rcu_reader;
    atomic_inc(&nthreadsrunning);
    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }

    while (goflag == GOFLAG_RUN) {
        rcu_read_lock();
        QLIST_FOREACH_RCU(el, &Q_list_head, entry) {
            n_reads_local++;
            if (goflag == GOFLAG_STOP) {
                break;
            }
        }
        rcu_read_unlock();

        g_usleep(100);
    }
    qemu_mutex_lock(&counts_mutex);
    n_reads += n_reads_local;
    qemu_mutex_unlock(&counts_mutex);

    rcu_unregister_thread();
    return NULL;
}


static void *rcu_q_updater(void *arg)
{
    int j, target_el;
    long long n_nodes_local = 0;
    long long n_updates_local = 0;
    long long n_removed_local = 0;
    struct list_element *el, *prev_el;

    *(struct rcu_reader_data **)arg = &rcu_reader;
    atomic_inc(&nthreadsrunning);
    while (goflag == GOFLAG_INIT) {
        g_usleep(1000);
    }

    while (goflag == GOFLAG_RUN) {
        target_el = select_random_el(RCU_Q_LEN);
        j = 0;
        /* FOREACH_RCU could work here but let's use both macros */
        QLIST_FOREACH_SAFE_RCU(prev_el, &Q_list_head, entry, el) {
            j++;
            if (target_el == j) {
                QLIST_REMOVE_RCU(prev_el, entry);
                /* may be more than one updater in the future */
                call_rcu1(&prev_el->rcu, reclaim_list_el);
                n_removed_local++;
                break;
            }
        }
        if (goflag == GOFLAG_STOP) {
            break;
        }
        target_el = select_random_el(RCU_Q_LEN);
        j = 0;
        QLIST_FOREACH_RCU(el, &Q_list_head, entry) {
            j++;
            if (target_el == j) {
                prev_el = g_new(struct list_element, 1);
                n_nodes += n_nodes_local;
                QLIST_INSERT_BEFORE_RCU(el, prev_el, entry);
                break;
            }
        }

        n_updates_local += 2;
        synchronize_rcu();
    }
    synchronize_rcu();
    qemu_mutex_lock(&counts_mutex);
    n_nodes += n_nodes_local;
    n_updates += n_updates_local;
    n_nodes_removed += n_removed_local;
    qemu_mutex_unlock(&counts_mutex);
    return NULL;
}

static void rcu_qtest_init(void)
{
    struct list_element *new_el;
    int i;
    nthreadsrunning = 0;
    srand(time(0));
    for (i = 0; i < RCU_Q_LEN; i++) {
        new_el = g_new(struct list_element, 1);
        QLIST_INSERT_HEAD_RCU(&Q_list_head, new_el, entry);
    }
    qemu_mutex_lock(&counts_mutex);
    n_nodes += RCU_Q_LEN;
    qemu_mutex_unlock(&counts_mutex);
}

static void rcu_qtest_run(int duration, int nreaders)
{
    int nthreads = nreaders + 1;
    while (atomic_read(&nthreadsrunning) < nthreads) {
        g_usleep(1000);
    }

    goflag = GOFLAG_RUN;
    sleep(duration);
    goflag = GOFLAG_STOP;
    wait_all_threads();
}


static void rcu_qtest(const char *test, int duration, int nreaders)
{
    int i;
    long long n_removed_local = 0;

    struct list_element *el, *prev_el;

    rcu_qtest_init();
    for (i = 0; i < nreaders; i++) {
        create_thread(rcu_q_reader);
    }
    create_thread(rcu_q_updater);
    rcu_qtest_run(duration, nreaders);

    QLIST_FOREACH_SAFE_RCU(prev_el, &Q_list_head, entry, el) {
        QLIST_REMOVE_RCU(prev_el, entry);
        call_rcu1(&prev_el->rcu, reclaim_list_el);
        n_removed_local++;
    }
    qemu_mutex_lock(&counts_mutex);
    n_nodes_removed += n_removed_local;
    qemu_mutex_unlock(&counts_mutex);
    synchronize_rcu();
    while (n_nodes_removed > n_reclaims) {
        g_usleep(100);
        synchronize_rcu();
    }
    if (g_test_in_charge) {
        g_assert_cmpint(n_nodes_removed, ==, n_reclaims);
    } else {
        printf("%s: %d readers; 1 updater; nodes read: "  \
               "%lld, nodes removed: %lld; nodes reclaimed: %lld\n",
               test, nthreadsrunning - 1, n_reads, n_nodes_removed, n_reclaims);
        exit(0);
    }
}

static void usage(int argc, char *argv[])
{
    fprintf(stderr, "Usage: %s duration nreaders\n", argv[0]);
    exit(-1);
}

static int gtest_seconds;

static void gtest_rcuq_one(void)
{
    rcu_qtest("rcuqtest", gtest_seconds / 4, 1);
}

static void gtest_rcuq_few(void)
{
    rcu_qtest("rcuqtest", gtest_seconds / 4, 5);
}

static void gtest_rcuq_many(void)
{
    rcu_qtest("rcuqtest", gtest_seconds / 2, 20);
}


int main(int argc, char *argv[])
{
    int duration = 0, readers = 0;

    qemu_mutex_init(&counts_mutex);
    if (argc >= 2) {
        if (argv[1][0] == '-') {
            g_test_init(&argc, &argv, NULL);
            if (g_test_quick()) {
                gtest_seconds = 4;
            } else {
                gtest_seconds = 20;
            }
            g_test_add_func("/rcu/qlist/single-threaded", gtest_rcuq_one);
            g_test_add_func("/rcu/qlist/short-few", gtest_rcuq_few);
            g_test_add_func("/rcu/qlist/long-many", gtest_rcuq_many);
            g_test_in_charge = 1;
            return g_test_run();
        }
        duration = strtoul(argv[1], NULL, 0);
    }
    if (argc >= 3) {
        readers = strtoul(argv[2], NULL, 0);
    }
    if (duration && readers) {
        rcu_qtest(argv[0], duration, readers);
        return 0;
    }

    usage(argc, argv);
    return -1;
}
