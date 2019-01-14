/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/host-utils.h"
#include "qemu/processor.h"

struct thread_info {
    uint64_t r;
    uint64_t accesses;
} QEMU_ALIGNED(64);

struct count {
    int64_t i64;
} QEMU_ALIGNED(64);

static QemuThread *threads;
static struct thread_info *th_info;
static unsigned int n_threads = 1;
static unsigned int n_ready_threads;
static struct count *counts;
static unsigned int duration = 1;
static unsigned int range = 1024;
static bool test_start;
static bool test_stop;

static const char commands_string[] =
    " -d = duration in seconds\n"
    " -n = number of threads\n"
    " -r = range (will be rounded up to pow2)";

static void usage_complete(char *argv[])
{
    fprintf(stderr, "Usage: %s [options]\n", argv[0]);
    fprintf(stderr, "options:\n%s\n", commands_string);
}

/*
 * From: https://en.wikipedia.org/wiki/Xorshift
 * This is faster than rand_r(), and gives us a wider range (RAND_MAX is only
 * guaranteed to be >= INT_MAX).
 */
static uint64_t xorshift64star(uint64_t x)
{
    x ^= x >> 12; /* a */
    x ^= x << 25; /* b */
    x ^= x >> 27; /* c */
    return x * UINT64_C(2685821657736338717);
}

static void *thread_func(void *arg)
{
    struct thread_info *info = arg;

    atomic_inc(&n_ready_threads);
    while (!atomic_read(&test_start)) {
        cpu_relax();
    }

    while (!atomic_read(&test_stop)) {
        unsigned int index;

        info->r = xorshift64star(info->r);
        index = info->r & (range - 1);
        atomic_read_i64(&counts[index].i64);
        info->accesses++;
    }
    return NULL;
}

static void run_test(void)
{
    unsigned int i;

    while (atomic_read(&n_ready_threads) != n_threads) {
        cpu_relax();
    }

    atomic_set(&test_start, true);
    g_usleep(duration * G_USEC_PER_SEC);
    atomic_set(&test_stop, true);

    for (i = 0; i < n_threads; i++) {
        qemu_thread_join(&threads[i]);
    }
}

static void create_threads(void)
{
    unsigned int i;

    threads = g_new(QemuThread, n_threads);
    th_info = g_new(struct thread_info, n_threads);
    counts = g_malloc0_n(range, sizeof(*counts));

    for (i = 0; i < n_threads; i++) {
        struct thread_info *info = &th_info[i];

        info->r = (i + 1) ^ time(NULL);
        info->accesses = 0;
        qemu_thread_create(&threads[i], NULL, thread_func, info,
                           QEMU_THREAD_JOINABLE);
    }
}

static void pr_params(void)
{
    printf("Parameters:\n");
    printf(" # of threads:      %u\n", n_threads);
    printf(" duration:          %u\n", duration);
    printf(" ops' range:        %u\n", range);
}

static void pr_stats(void)
{
    unsigned long long val = 0;
    double tx;
    int i;

    for (i = 0; i < n_threads; i++) {
        val += th_info[i].accesses;
    }
    tx = val / duration / 1e6;

    printf("Results:\n");
    printf("Duration:            %u s\n", duration);
    printf(" Throughput:         %.2f Mops/s\n", tx);
    printf(" Throughput/thread:  %.2f Mops/s/thread\n", tx / n_threads);
}

static void parse_args(int argc, char *argv[])
{
    int c;

    for (;;) {
        c = getopt(argc, argv, "hd:n:r:");
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'h':
            usage_complete(argv);
            exit(0);
        case 'd':
            duration = atoi(optarg);
            break;
        case 'n':
            n_threads = atoi(optarg);
            break;
        case 'r':
            range = pow2ceil(atoi(optarg));
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    pr_params();
    create_threads();
    run_test();
    pr_stats();
    return 0;
}
