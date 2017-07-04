/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/processor.h"
#include "qemu/atomic.h"
#include "qemu/qht.h"
#include "qemu/rcu.h"
#include "exec/tb-hash-xx.h"

struct thread_stats {
    size_t rd;
    size_t not_rd;
    size_t in;
    size_t not_in;
    size_t rm;
    size_t not_rm;
    size_t rz;
    size_t not_rz;
};

struct thread_info {
    void (*func)(struct thread_info *);
    struct thread_stats stats;
    uint64_t r;
    bool write_op; /* writes alternate between insertions and removals */
    bool resize_down;
} QEMU_ALIGNED(64); /* avoid false sharing among threads */

static struct qht ht;
static QemuThread *rw_threads;

#define DEFAULT_RANGE (4096)
#define DEFAULT_QHT_N_ELEMS DEFAULT_RANGE

static unsigned int duration = 1;
static unsigned int n_rw_threads = 1;
static unsigned long lookup_range = DEFAULT_RANGE;
static unsigned long update_range = DEFAULT_RANGE;
static size_t init_range = DEFAULT_RANGE;
static size_t init_size = DEFAULT_RANGE;
static size_t n_ready_threads;
static long populate_offset;
static long *keys;

static size_t resize_min;
static size_t resize_max;
static struct thread_info *rz_info;
static unsigned long resize_delay = 1000;
static double resize_rate; /* 0.0 to 1.0 */
static unsigned int n_rz_threads = 1;
static QemuThread *rz_threads;

static double update_rate; /* 0.0 to 1.0 */
static uint64_t update_threshold;
static uint64_t resize_threshold;

static size_t qht_n_elems = DEFAULT_QHT_N_ELEMS;
static int qht_mode;

static bool test_start;
static bool test_stop;

static struct thread_info *rw_info;

static const char commands_string[] =
    " -d = duration, in seconds\n"
    " -n = number of threads\n"
    "\n"
    " -o = offset at which keys start\n"
    "\n"
    " -g = set -s,-k,-K,-l,-r to the same value\n"
    " -s = initial size hint\n"
    " -k = initial number of keys\n"
    " -K = initial range of keys (will be rounded up to pow2)\n"
    " -l = lookup range of keys (will be rounded up to pow2)\n"
    " -r = update range of keys (will be rounded up to pow2)\n"
    "\n"
    " -u = update rate (0.0 to 100.0), 50/50 split of insertions/removals\n"
    "\n"
    " -R = enable auto-resize\n"
    " -S = resize rate (0.0 to 100.0)\n"
    " -D = delay (in us) between potential resizes\n"
    " -N = number of resize threads";

static void usage_complete(int argc, char *argv[])
{
    fprintf(stderr, "Usage: %s [options]\n", argv[0]);
    fprintf(stderr, "options:\n%s\n", commands_string);
    exit(-1);
}

static bool is_equal(const void *obj, const void *userp)
{
    const long *a = obj;
    const long *b = userp;

    return *a == *b;
}

static inline uint32_t h(unsigned long v)
{
    return tb_hash_func6(v, 0, 0, 0);
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

static void do_rz(struct thread_info *info)
{
    struct thread_stats *stats = &info->stats;

    if (info->r < resize_threshold) {
        size_t size = info->resize_down ? resize_min : resize_max;
        bool resized;

        resized = qht_resize(&ht, size);
        info->resize_down = !info->resize_down;

        if (resized) {
            stats->rz++;
        } else {
            stats->not_rz++;
        }
    }
    g_usleep(resize_delay);
}

static void do_rw(struct thread_info *info)
{
    struct thread_stats *stats = &info->stats;
    uint32_t hash;
    long *p;

    if (info->r >= update_threshold) {
        bool read;

        p = &keys[info->r & (lookup_range - 1)];
        hash = h(*p);
        read = qht_lookup(&ht, is_equal, p, hash);
        if (read) {
            stats->rd++;
        } else {
            stats->not_rd++;
        }
    } else {
        p = &keys[info->r & (update_range - 1)];
        hash = h(*p);
        if (info->write_op) {
            bool written = false;

            if (qht_lookup(&ht, is_equal, p, hash) == NULL) {
                written = qht_insert(&ht, p, hash);
            }
            if (written) {
                stats->in++;
            } else {
                stats->not_in++;
            }
        } else {
            bool removed = false;

            if (qht_lookup(&ht, is_equal, p, hash)) {
                removed = qht_remove(&ht, p, hash);
            }
            if (removed) {
                stats->rm++;
            } else {
                stats->not_rm++;
            }
        }
        info->write_op = !info->write_op;
    }
}

static void *thread_func(void *p)
{
    struct thread_info *info = p;

    rcu_register_thread();

    atomic_inc(&n_ready_threads);
    while (!atomic_read(&test_start)) {
        cpu_relax();
    }

    rcu_read_lock();
    while (!atomic_read(&test_stop)) {
        info->r = xorshift64star(info->r);
        info->func(info);
    }
    rcu_read_unlock();

    rcu_unregister_thread();
    return NULL;
}

/* sets everything except info->func */
static void prepare_thread_info(struct thread_info *info, int i)
{
    /* seed for the RNG; each thread should have a different one */
    info->r = (i + 1) ^ time(NULL);
    /* the first update will be a write */
    info->write_op = true;
    /* the first resize will be down */
    info->resize_down = true;

    memset(&info->stats, 0, sizeof(info->stats));
}

static void
th_create_n(QemuThread **threads, struct thread_info **infos, const char *name,
            void (*func)(struct thread_info *), int offset, int n)
{
    struct thread_info *info;
    QemuThread *th;
    int i;

    th = g_malloc(sizeof(*th) * n);
    *threads = th;

    info = qemu_memalign(64, sizeof(*info) * n);
    *infos = info;

    for (i = 0; i < n; i++) {
        prepare_thread_info(&info[i], offset + i);
        info[i].func = func;
        qemu_thread_create(&th[i], name, thread_func, &info[i],
                           QEMU_THREAD_JOINABLE);
    }
}

static void create_threads(void)
{
    th_create_n(&rw_threads, &rw_info, "rw", do_rw, 0, n_rw_threads);
    th_create_n(&rz_threads, &rz_info, "rz", do_rz, n_rw_threads, n_rz_threads);
}

static void pr_params(void)
{
    printf("Parameters:\n");
    printf(" duration:          %d s\n", duration);
    printf(" # of threads:      %u\n", n_rw_threads);
    printf(" initial # of keys: %zu\n", init_size);
    printf(" initial size hint: %zu\n", qht_n_elems);
    printf(" auto-resize:       %s\n",
           qht_mode & QHT_MODE_AUTO_RESIZE ? "on" : "off");
    if (resize_rate) {
        printf(" resize_rate:       %f%%\n", resize_rate * 100.0);
        printf(" resize range:      %zu-%zu\n", resize_min, resize_max);
        printf(" # resize threads   %u\n", n_rz_threads);
    }
    printf(" update rate:       %f%%\n", update_rate * 100.0);
    printf(" offset:            %ld\n", populate_offset);
    printf(" initial key range: %zu\n", init_range);
    printf(" lookup range:      %lu\n", lookup_range);
    printf(" update range:      %lu\n", update_range);
}

static void do_threshold(double rate, uint64_t *threshold)
{
    if (rate == 1.0) {
        *threshold = UINT64_MAX;
    } else {
        *threshold = rate * UINT64_MAX;
    }
}

static void htable_init(void)
{
    unsigned long n = MAX(init_range, update_range);
    uint64_t r = time(NULL);
    size_t retries = 0;
    size_t i;

    /* avoid allocating memory later by allocating all the keys now */
    keys = g_malloc(sizeof(*keys) * n);
    for (i = 0; i < n; i++) {
        keys[i] = populate_offset + i;
    }

    /* some sanity checks */
    g_assert_cmpuint(lookup_range, <=, n);

    /* compute thresholds */
    do_threshold(update_rate, &update_threshold);
    do_threshold(resize_rate, &resize_threshold);

    if (resize_rate) {
        resize_min = n / 2;
        resize_max = n;
        assert(resize_min < resize_max);
    } else {
        n_rz_threads = 0;
    }

    /* initialize the hash table */
    qht_init(&ht, qht_n_elems, qht_mode);
    assert(init_size <= init_range);

    pr_params();

    fprintf(stderr, "Initialization: populating %zu items...", init_size);
    for (i = 0; i < init_size; i++) {
        for (;;) {
            uint32_t hash;
            long *p;

            r = xorshift64star(r);
            p = &keys[r & (init_range - 1)];
            hash = h(*p);
            if (qht_insert(&ht, p, hash)) {
                break;
            }
            retries++;
        }
    }
    fprintf(stderr, " populated after %zu retries\n", retries);
}

static void add_stats(struct thread_stats *s, struct thread_info *info, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        struct thread_stats *stats = &info[i].stats;

        s->rd += stats->rd;
        s->not_rd += stats->not_rd;

        s->in += stats->in;
        s->not_in += stats->not_in;

        s->rm += stats->rm;
        s->not_rm += stats->not_rm;

        s->rz += stats->rz;
        s->not_rz += stats->not_rz;
    }
}

static void pr_stats(void)
{
    struct thread_stats s = {};
    double tx;

    add_stats(&s, rw_info, n_rw_threads);
    add_stats(&s, rz_info, n_rz_threads);

    printf("Results:\n");

    if (resize_rate) {
        printf(" Resizes:           %zu (%.2f%% of %zu)\n",
               s.rz, (double)s.rz / (s.rz + s.not_rz) * 100, s.rz + s.not_rz);
    }

    printf(" Read:              %.2f M (%.2f%% of %.2fM)\n",
           (double)s.rd / 1e6,
           (double)s.rd / (s.rd + s.not_rd) * 100,
           (double)(s.rd + s.not_rd) / 1e6);
    printf(" Inserted:          %.2f M (%.2f%% of %.2fM)\n",
           (double)s.in / 1e6,
           (double)s.in / (s.in + s.not_in) * 100,
           (double)(s.in + s.not_in) / 1e6);
    printf(" Removed:           %.2f M (%.2f%% of %.2fM)\n",
           (double)s.rm / 1e6,
           (double)s.rm / (s.rm + s.not_rm) * 100,
           (double)(s.rm + s.not_rm) / 1e6);

    tx = (s.rd + s.not_rd + s.in + s.not_in + s.rm + s.not_rm) / 1e6 / duration;
    printf(" Throughput:        %.2f MT/s\n", tx);
    printf(" Throughput/thread: %.2f MT/s/thread\n", tx / n_rw_threads);
}

static void run_test(void)
{
    unsigned int remaining;
    int i;

    while (atomic_read(&n_ready_threads) != n_rw_threads + n_rz_threads) {
        cpu_relax();
    }
    atomic_set(&test_start, true);
    do {
        remaining = sleep(duration);
    } while (remaining);
    atomic_set(&test_stop, true);

    for (i = 0; i < n_rw_threads; i++) {
        qemu_thread_join(&rw_threads[i]);
    }
    for (i = 0; i < n_rz_threads; i++) {
        qemu_thread_join(&rz_threads[i]);
    }
}

static void parse_args(int argc, char *argv[])
{
    int c;

    for (;;) {
        c = getopt(argc, argv, "d:D:g:k:K:l:hn:N:o:r:Rs:S:u:");
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'd':
            duration = atoi(optarg);
            break;
        case 'D':
            resize_delay = atol(optarg);
            break;
        case 'g':
            init_range = pow2ceil(atol(optarg));
            lookup_range = pow2ceil(atol(optarg));
            update_range = pow2ceil(atol(optarg));
            qht_n_elems = atol(optarg);
            init_size = atol(optarg);
            break;
        case 'h':
            usage_complete(argc, argv);
            exit(0);
        case 'k':
            init_size = atol(optarg);
            break;
        case 'K':
            init_range = pow2ceil(atol(optarg));
            break;
        case 'l':
            lookup_range = pow2ceil(atol(optarg));
            break;
        case 'n':
            n_rw_threads = atoi(optarg);
            break;
        case 'N':
            n_rz_threads = atoi(optarg);
            break;
        case 'o':
            populate_offset = atol(optarg);
            break;
        case 'r':
            update_range = pow2ceil(atol(optarg));
            break;
        case 'R':
            qht_mode |= QHT_MODE_AUTO_RESIZE;
            break;
        case 's':
            qht_n_elems = atol(optarg);
            break;
        case 'S':
            resize_rate = atof(optarg) / 100.0;
            if (resize_rate > 1.0) {
                resize_rate = 1.0;
            }
            break;
        case 'u':
            update_rate = atof(optarg) / 100.0;
            if (update_rate > 1.0) {
                update_rate = 1.0;
            }
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    htable_init();
    create_threads();
    run_test();
    pr_stats();
    return 0;
}
