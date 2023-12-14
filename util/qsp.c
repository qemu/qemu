/*
 * qsp.c - QEMU Synchronization Profiler
 *
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * QSP profiles the time spent in synchronization primitives, which can
 * help diagnose performance problems, e.g. scalability issues when
 * contention is high.
 *
 * The primitives currently supported are mutexes, recursive mutexes and
 * condition variables. Note that not all related functions are intercepted;
 * instead we profile only those functions that can have a performance impact,
 * either due to blocking (e.g. cond_wait, mutex_lock) or cache line
 * contention (e.g. mutex_lock, mutex_trylock).
 *
 * QSP's design focuses on speed and scalability. This is achieved
 * by having threads do their profiling entirely on thread-local data.
 * The appropriate thread-local data is found via a QHT, i.e. a concurrent hash
 * table. To aggregate data in order to generate a report, we iterate over
 * all entries in the hash table. Depending on the number of threads and
 * synchronization objects this might be expensive, but note that it is
 * very rarely called -- reports are generated only when requested by users.
 *
 * Reports are generated as a table where each row represents a call site. A
 * call site is the triplet formed by the __file__ and __LINE__ of the caller
 * as well as the address of the "object" (i.e. mutex, rec. mutex or condvar)
 * being operated on. Optionally, call sites that operate on different objects
 * of the same type can be coalesced, which can be particularly useful when
 * profiling dynamically-allocated objects.
 *
 * Alternative designs considered:
 *
 * - Use an off-the-shelf profiler such as mutrace. This is not a viable option
 *   for us because QEMU has __malloc_hook set (by one of the libraries it
 *   uses); leaving this hook unset is required to avoid deadlock in mutrace.
 *
 * - Use a glib HT for each thread, protecting each HT with its own lock.
 *   This isn't simpler than the current design, and is 10% slower in the
 *   atomic_add-bench microbenchmark (-m option).
 *
 * - For reports, just use a binary tree as we aggregate data, instead of having
 *   an intermediate hash table. This would simplify the code only slightly, but
 *   would perform badly if there were many threads and objects to track.
 *
 * - Wrap operations on qsp entries with RCU read-side critical sections, so
 *   that qsp_reset() can delete entries. Unfortunately, the overhead of calling
 *   rcu_read_lock/unlock slows down atomic_add-bench -m by 24%. Having
 *   a snapshot that is updated on qsp_reset() avoids this overhead.
 *
 * Related Work:
 * - Lennart Poettering's mutrace: http://0pointer.de/blog/projects/mutrace.html
 * - Lozi, David, Thomas, Lawall and Muller. "Remote Core Locking: Migrating
 *   Critical-Section Execution to Improve the Performance of Multithreaded
 *   Applications", USENIX ATC'12.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "qemu/qht.h"
#include "qemu/rcu.h"
#include "qemu/xxhash.h"

enum QSPType {
    QSP_MUTEX,
    QSP_BQL_MUTEX,
    QSP_REC_MUTEX,
    QSP_CONDVAR,
};

struct QSPCallSite {
    const void *obj;
    const char *file; /* i.e. __FILE__; shortened later */
    int line;
    enum QSPType type;
};
typedef struct QSPCallSite QSPCallSite;

struct QSPEntry {
    void *thread_ptr;
    const QSPCallSite *callsite;
    aligned_uint64_t n_acqs;
    aligned_uint64_t ns;
    unsigned int n_objs; /* count of coalesced objs; only used for reporting */
};
typedef struct QSPEntry QSPEntry;

struct QSPSnapshot {
    struct rcu_head rcu;
    struct qht ht;
};
typedef struct QSPSnapshot QSPSnapshot;

/* initial sizing for hash tables */
#define QSP_INITIAL_SIZE 64

/* If this file is moved, QSP_REL_PATH should be updated accordingly */
#define QSP_REL_PATH "util/qsp.c"

/* this file's full path. Used to present all call sites with relative paths */
static size_t qsp_qemu_path_len;

/* the address of qsp_thread gives us a unique 'thread ID' */
static __thread int qsp_thread;

/*
 * Call sites are the same for all threads, so we track them in a separate hash
 * table to save memory.
 */
static struct qht qsp_callsite_ht;

static struct qht qsp_ht;
static QSPSnapshot *qsp_snapshot;
static bool qsp_initialized, qsp_initializing;

static const char * const qsp_typenames[] = {
    [QSP_MUTEX]     = "mutex",
    [QSP_BQL_MUTEX] = "BQL mutex",
    [QSP_REC_MUTEX] = "rec_mutex",
    [QSP_CONDVAR]   = "condvar",
};

QemuMutexLockFunc qemu_bql_mutex_lock_func = qemu_mutex_lock_impl;
QemuMutexLockFunc qemu_mutex_lock_func = qemu_mutex_lock_impl;
QemuMutexTrylockFunc qemu_mutex_trylock_func = qemu_mutex_trylock_impl;
QemuRecMutexLockFunc qemu_rec_mutex_lock_func = qemu_rec_mutex_lock_impl;
QemuRecMutexTrylockFunc qemu_rec_mutex_trylock_func =
    qemu_rec_mutex_trylock_impl;
QemuCondWaitFunc qemu_cond_wait_func = qemu_cond_wait_impl;
QemuCondTimedWaitFunc qemu_cond_timedwait_func = qemu_cond_timedwait_impl;

/*
 * It pays off to _not_ hash callsite->file; hashing a string is slow, and
 * without it we still get a pretty unique hash.
 */
static inline
uint32_t do_qsp_callsite_hash(const QSPCallSite *callsite, uint64_t ab)
{
    uint64_t cd = (uint64_t)(uintptr_t)callsite->obj;
    uint32_t e = callsite->line;
    uint32_t f = callsite->type;

    return qemu_xxhash8(ab, cd, 0, e, f);
}

static inline
uint32_t qsp_callsite_hash(const QSPCallSite *callsite)
{
    return do_qsp_callsite_hash(callsite, 0);
}

static inline uint32_t do_qsp_entry_hash(const QSPEntry *entry, uint64_t a)
{
    return do_qsp_callsite_hash(entry->callsite, a);
}

static uint32_t qsp_entry_hash(const QSPEntry *entry)
{
    return do_qsp_entry_hash(entry, (uint64_t)(uintptr_t)entry->thread_ptr);
}

static uint32_t qsp_entry_no_thread_hash(const QSPEntry *entry)
{
    return do_qsp_entry_hash(entry, 0);
}

/* without the objects we need to hash the file name to get a decent hash */
static uint32_t qsp_entry_no_thread_obj_hash(const QSPEntry *entry)
{
    const QSPCallSite *callsite = entry->callsite;
    uint64_t ab = g_str_hash(callsite->file);
    uint64_t cd = callsite->line;
    uint32_t e = callsite->type;

    return qemu_xxhash5(ab, cd, e);
}

static bool qsp_callsite_cmp(const void *ap, const void *bp)
{
    const QSPCallSite *a = ap;
    const QSPCallSite *b = bp;

    return a == b ||
        (a->obj == b->obj &&
         a->line == b->line &&
         a->type == b->type &&
         (a->file == b->file || !strcmp(a->file, b->file)));
}

static bool qsp_callsite_no_obj_cmp(const void *ap, const void *bp)
{
    const QSPCallSite *a = ap;
    const QSPCallSite *b = bp;

    return a == b ||
        (a->line == b->line &&
         a->type == b->type &&
         (a->file == b->file || !strcmp(a->file, b->file)));
}

static bool qsp_entry_no_thread_cmp(const void *ap, const void *bp)
{
    const QSPEntry *a = ap;
    const QSPEntry *b = bp;

    return qsp_callsite_cmp(a->callsite, b->callsite);
}

static bool qsp_entry_no_thread_obj_cmp(const void *ap, const void *bp)
{
    const QSPEntry *a = ap;
    const QSPEntry *b = bp;

    return qsp_callsite_no_obj_cmp(a->callsite, b->callsite);
}

static bool qsp_entry_cmp(const void *ap, const void *bp)
{
    const QSPEntry *a = ap;
    const QSPEntry *b = bp;

    return a->thread_ptr == b->thread_ptr &&
        qsp_callsite_cmp(a->callsite, b->callsite);
}

/*
 * Normally we'd call this from a constructor function, but we want it to work
 * via libutil as well.
 */
static void qsp_do_init(void)
{
    /* make sure this file's path in the tree is up to date with QSP_REL_PATH */
    g_assert(strstr(__FILE__, QSP_REL_PATH));
    qsp_qemu_path_len = strlen(__FILE__) - strlen(QSP_REL_PATH);

    qht_init(&qsp_ht, qsp_entry_cmp, QSP_INITIAL_SIZE,
             QHT_MODE_AUTO_RESIZE | QHT_MODE_RAW_MUTEXES);
    qht_init(&qsp_callsite_ht, qsp_callsite_cmp, QSP_INITIAL_SIZE,
             QHT_MODE_AUTO_RESIZE | QHT_MODE_RAW_MUTEXES);
}

static __attribute__((noinline)) void qsp_init__slowpath(void)
{
    if (qatomic_cmpxchg(&qsp_initializing, false, true) == false) {
        qsp_do_init();
        qatomic_set(&qsp_initialized, true);
    } else {
        while (!qatomic_read(&qsp_initialized)) {
            cpu_relax();
        }
    }
}

/* qsp_init() must be called from _all_ exported functions */
static inline void qsp_init(void)
{
    if (likely(qatomic_read(&qsp_initialized))) {
        return;
    }
    qsp_init__slowpath();
}

static QSPCallSite *qsp_callsite_find(const QSPCallSite *orig)
{
    QSPCallSite *callsite;
    uint32_t hash;

    hash = qsp_callsite_hash(orig);
    callsite = qht_lookup(&qsp_callsite_ht, orig, hash);
    if (callsite == NULL) {
        void *existing = NULL;

        callsite = g_new(QSPCallSite, 1);
        memcpy(callsite, orig, sizeof(*callsite));
        qht_insert(&qsp_callsite_ht, callsite, hash, &existing);
        if (unlikely(existing)) {
            g_free(callsite);
            callsite = existing;
        }
    }
    return callsite;
}

static QSPEntry *
qsp_entry_create(struct qht *ht, const QSPEntry *entry, uint32_t hash)
{
    QSPEntry *e;
    void *existing = NULL;

    e = g_new0(QSPEntry, 1);
    e->thread_ptr = entry->thread_ptr;
    e->callsite = qsp_callsite_find(entry->callsite);

    qht_insert(ht, e, hash, &existing);
    if (unlikely(existing)) {
        g_free(e);
        e = existing;
    }
    return e;
}

static QSPEntry *
qsp_entry_find(struct qht *ht, const QSPEntry *entry, uint32_t hash)
{
    QSPEntry *e;

    e = qht_lookup(ht, entry, hash);
    if (e == NULL) {
        e = qsp_entry_create(ht, entry, hash);
    }
    return e;
}

/*
 * Note: Entries are never removed, so callers do not have to be in an RCU
 * read-side critical section.
 */
static QSPEntry *qsp_entry_get(const void *obj, const char *file, int line,
                               enum QSPType type)
{
    QSPCallSite callsite = {
        .obj = obj,
        .file = file,
        .line = line,
        .type = type,
    };
    QSPEntry orig;
    uint32_t hash;

    qsp_init();

    orig.thread_ptr = &qsp_thread;
    orig.callsite = &callsite;

    hash = qsp_entry_hash(&orig);
    return qsp_entry_find(&qsp_ht, &orig, hash);
}

/*
 * @e is in the global hash table; it is only written to by the current thread,
 * so we write to it atomically (as in "write once") to prevent torn reads.
 */
static inline void do_qsp_entry_record(QSPEntry *e, int64_t delta, bool acq)
{
    qatomic_set_u64(&e->ns, e->ns + delta);
    if (acq) {
        qatomic_set_u64(&e->n_acqs, e->n_acqs + 1);
    }
}

static inline void qsp_entry_record(QSPEntry *e, int64_t delta)
{
    do_qsp_entry_record(e, delta, true);
}

#define QSP_GEN_VOID(type_, qsp_t_, func_, impl_)                       \
    static void func_(type_ *obj, const char *file, int line)           \
    {                                                                   \
        QSPEntry *e;                                                    \
        int64_t t0, t1;                                                 \
                                                                        \
        t0 = get_clock();                                               \
        impl_(obj, file, line);                                         \
        t1 = get_clock();                                               \
                                                                        \
        e = qsp_entry_get(obj, file, line, qsp_t_);                     \
        qsp_entry_record(e, t1 - t0);                                   \
    }

#define QSP_GEN_RET1(type_, qsp_t_, func_, impl_)                       \
    static int func_(type_ *obj, const char *file, int line)            \
    {                                                                   \
        QSPEntry *e;                                                    \
        int64_t t0, t1;                                                 \
        int err;                                                        \
                                                                        \
        t0 = get_clock();                                               \
        err = impl_(obj, file, line);                                   \
        t1 = get_clock();                                               \
                                                                        \
        e = qsp_entry_get(obj, file, line, qsp_t_);                     \
        do_qsp_entry_record(e, t1 - t0, !err);                          \
        return err;                                                     \
    }

QSP_GEN_VOID(QemuMutex, QSP_BQL_MUTEX, qsp_bql_mutex_lock, qemu_mutex_lock_impl)
QSP_GEN_VOID(QemuMutex, QSP_MUTEX, qsp_mutex_lock, qemu_mutex_lock_impl)
QSP_GEN_RET1(QemuMutex, QSP_MUTEX, qsp_mutex_trylock, qemu_mutex_trylock_impl)

QSP_GEN_VOID(QemuRecMutex, QSP_REC_MUTEX, qsp_rec_mutex_lock,
             qemu_rec_mutex_lock_impl)
QSP_GEN_RET1(QemuRecMutex, QSP_REC_MUTEX, qsp_rec_mutex_trylock,
             qemu_rec_mutex_trylock_impl)

#undef QSP_GEN_RET1
#undef QSP_GEN_VOID

static void
qsp_cond_wait(QemuCond *cond, QemuMutex *mutex, const char *file, int line)
{
    QSPEntry *e;
    int64_t t0, t1;

    t0 = get_clock();
    qemu_cond_wait_impl(cond, mutex, file, line);
    t1 = get_clock();

    e = qsp_entry_get(cond, file, line, QSP_CONDVAR);
    qsp_entry_record(e, t1 - t0);
}

static bool
qsp_cond_timedwait(QemuCond *cond, QemuMutex *mutex, int ms,
                   const char *file, int line)
{
    QSPEntry *e;
    int64_t t0, t1;
    bool ret;

    t0 = get_clock();
    ret = qemu_cond_timedwait_impl(cond, mutex, ms, file, line);
    t1 = get_clock();

    e = qsp_entry_get(cond, file, line, QSP_CONDVAR);
    qsp_entry_record(e, t1 - t0);
    return ret;
}

bool qsp_is_enabled(void)
{
    return qatomic_read(&qemu_mutex_lock_func) == qsp_mutex_lock;
}

void qsp_enable(void)
{
    qatomic_set(&qemu_mutex_lock_func, qsp_mutex_lock);
    qatomic_set(&qemu_mutex_trylock_func, qsp_mutex_trylock);
    qatomic_set(&qemu_bql_mutex_lock_func, qsp_bql_mutex_lock);
    qatomic_set(&qemu_rec_mutex_lock_func, qsp_rec_mutex_lock);
    qatomic_set(&qemu_rec_mutex_trylock_func, qsp_rec_mutex_trylock);
    qatomic_set(&qemu_cond_wait_func, qsp_cond_wait);
    qatomic_set(&qemu_cond_timedwait_func, qsp_cond_timedwait);
}

void qsp_disable(void)
{
    qatomic_set(&qemu_mutex_lock_func, qemu_mutex_lock_impl);
    qatomic_set(&qemu_mutex_trylock_func, qemu_mutex_trylock_impl);
    qatomic_set(&qemu_bql_mutex_lock_func, qemu_mutex_lock_impl);
    qatomic_set(&qemu_rec_mutex_lock_func, qemu_rec_mutex_lock_impl);
    qatomic_set(&qemu_rec_mutex_trylock_func, qemu_rec_mutex_trylock_impl);
    qatomic_set(&qemu_cond_wait_func, qemu_cond_wait_impl);
    qatomic_set(&qemu_cond_timedwait_func, qemu_cond_timedwait_impl);
}

static gint qsp_tree_cmp(gconstpointer ap, gconstpointer bp, gpointer up)
{
    const QSPEntry *a = ap;
    const QSPEntry *b = bp;
    enum QSPSortBy sort_by = *(enum QSPSortBy *)up;
    const QSPCallSite *ca;
    const QSPCallSite *cb;

    switch (sort_by) {
    case QSP_SORT_BY_TOTAL_WAIT_TIME:
        if (a->ns > b->ns) {
            return -1;
        } else if (a->ns < b->ns) {
            return 1;
        }
        break;
    case QSP_SORT_BY_AVG_WAIT_TIME:
    {
        double avg_a = a->n_acqs ? a->ns / a->n_acqs : 0;
        double avg_b = b->n_acqs ? b->ns / b->n_acqs : 0;

        if (avg_a > avg_b) {
            return -1;
        } else if (avg_a < avg_b) {
            return 1;
        }
        break;
    }
    default:
        g_assert_not_reached();
    }

    ca = a->callsite;
    cb = b->callsite;
    /* Break the tie with the object's address */
    if (ca->obj < cb->obj) {
        return -1;
    } else if (ca->obj > cb->obj) {
        return 1;
    } else {
        int cmp;

        /* same obj. Break the tie with the callsite's file */
        cmp = strcmp(ca->file, cb->file);
        if (cmp) {
            return cmp;
        }
        /* same callsite file. Break the tie with the callsite's line */
        g_assert(ca->line != cb->line);
        if (ca->line < cb->line) {
            return -1;
        } else if (ca->line > cb->line) {
            return 1;
        } else {
            /* break the tie with the callsite's type */
            return cb->type - ca->type;
        }
    }
}

static void qsp_sort(void *p, uint32_t h, void *userp)
{
    QSPEntry *e = p;
    GTree *tree = userp;

    g_tree_insert(tree, e, NULL);
}

static void qsp_aggregate(void *p, uint32_t h, void *up)
{
    struct qht *ht = up;
    const QSPEntry *e = p;
    QSPEntry *agg;
    uint32_t hash;

    hash = qsp_entry_no_thread_hash(e);
    agg = qsp_entry_find(ht, e, hash);
    /*
     * The entry is in the global hash table; read from it atomically (as in
     * "read once").
     */
    agg->ns += qatomic_read_u64(&e->ns);
    agg->n_acqs += qatomic_read_u64(&e->n_acqs);
}

static void qsp_iter_diff(void *p, uint32_t hash, void *htp)
{
    struct qht *ht = htp;
    QSPEntry *old = p;
    QSPEntry *new;

    new = qht_lookup(ht, old, hash);
    /* entries are never deleted, so we must have this one */
    g_assert(new != NULL);
    /* our reading of the stats happened after the snapshot was taken */
    g_assert(new->n_acqs >= old->n_acqs);
    g_assert(new->ns >= old->ns);

    new->n_acqs -= old->n_acqs;
    new->ns -= old->ns;

    /* No point in reporting an empty entry */
    if (new->n_acqs == 0 && new->ns == 0) {
        bool removed = qht_remove(ht, new, hash);

        g_assert(removed);
        g_free(new);
    }
}

static void qsp_diff(struct qht *orig, struct qht *new)
{
    qht_iter(orig, qsp_iter_diff, new);
}

static void qsp_iter_callsite_coalesce(void *p, uint32_t h, void *htp)
{
    struct qht *ht = htp;
    QSPEntry *old = p;
    QSPEntry *e;
    uint32_t hash;

    hash = qsp_entry_no_thread_obj_hash(old);
    e = qht_lookup(ht, old, hash);
    if (e == NULL) {
        e = qsp_entry_create(ht, old, hash);
        e->n_objs = 1;
    } else if (e->callsite->obj != old->callsite->obj) {
        e->n_objs++;
    }
    e->ns += old->ns;
    e->n_acqs += old->n_acqs;
}

static void qsp_ht_delete(void *p, uint32_t h, void *htp)
{
    g_free(p);
}

static void qsp_mktree(GTree *tree, bool callsite_coalesce)
{
    struct qht ht, coalesce_ht;
    struct qht *htp;

    /*
     * First, see if there's a prior snapshot, so that we read the global hash
     * table _after_ the snapshot has been created, which guarantees that
     * the entries we'll read will be a superset of the snapshot's entries.
     *
     * We must remain in an RCU read-side critical section until we're done
     * with the snapshot.
     */
    WITH_RCU_READ_LOCK_GUARD() {
        QSPSnapshot *snap = qatomic_rcu_read(&qsp_snapshot);

        /* Aggregate all results from the global hash table into a local one */
        qht_init(&ht, qsp_entry_no_thread_cmp, QSP_INITIAL_SIZE,
                 QHT_MODE_AUTO_RESIZE | QHT_MODE_RAW_MUTEXES);
        qht_iter(&qsp_ht, qsp_aggregate, &ht);

        /* compute the difference wrt the snapshot, if any */
        if (snap) {
            qsp_diff(&snap->ht, &ht);
        }
    }

    htp = &ht;
    if (callsite_coalesce) {
        qht_init(&coalesce_ht, qsp_entry_no_thread_obj_cmp, QSP_INITIAL_SIZE,
                 QHT_MODE_AUTO_RESIZE | QHT_MODE_RAW_MUTEXES);
        qht_iter(&ht, qsp_iter_callsite_coalesce, &coalesce_ht);

        /* free the previous hash table, and point htp to coalesce_ht */
        qht_iter(&ht, qsp_ht_delete, NULL);
        qht_destroy(&ht);
        htp = &coalesce_ht;
    }

    /* sort the hash table elements by using a tree */
    qht_iter(htp, qsp_sort, tree);

    /* free the hash table, but keep the elements (those are in the tree now) */
    qht_destroy(htp);
}

/* free string with g_free */
static char *qsp_at(const QSPCallSite *callsite)
{
    GString *s = g_string_new(NULL);
    const char *shortened;

    /* remove the absolute path to qemu */
    if (unlikely(strlen(callsite->file) < qsp_qemu_path_len)) {
        shortened = callsite->file;
    } else {
        shortened = callsite->file + qsp_qemu_path_len;
    }
    g_string_append_printf(s, "%s:%u", shortened, callsite->line);
    return g_string_free(s, FALSE);
}

struct QSPReportEntry {
    const void *obj;
    char *callsite_at;
    const char *typename;
    double time_s;
    double ns_avg;
    uint64_t n_acqs;
    unsigned int n_objs;
};
typedef struct QSPReportEntry QSPReportEntry;

struct QSPReport {
    QSPReportEntry *entries;
    size_t n_entries;
    size_t max_n_entries;
};
typedef struct QSPReport QSPReport;

static gboolean qsp_tree_report(gpointer key, gpointer value, gpointer udata)
{
    const QSPEntry *e = key;
    QSPReport *report = udata;
    QSPReportEntry *entry;

    if (report->n_entries == report->max_n_entries) {
        return TRUE;
    }
    entry = &report->entries[report->n_entries];
    report->n_entries++;

    entry->obj = e->callsite->obj;
    entry->n_objs = e->n_objs;
    entry->callsite_at = qsp_at(e->callsite);
    entry->typename = qsp_typenames[e->callsite->type];
    entry->time_s = e->ns * 1e-9;
    entry->n_acqs = e->n_acqs;
    entry->ns_avg = e->n_acqs ? e->ns / e->n_acqs : 0;
    return FALSE;
}

static void pr_report(const QSPReport *rep)
{
    char *dashes;
    size_t max_len = 0;
    int callsite_len = 0;
    int callsite_rspace;
    int n_dashes;
    size_t i;

    /* find out the maximum length of all 'callsite' fields */
    for (i = 0; i < rep->n_entries; i++) {
        const QSPReportEntry *e = &rep->entries[i];
        size_t len = strlen(e->callsite_at);

        if (len > max_len) {
            max_len = len;
        }
    }

    callsite_len = MAX(max_len, strlen("Call site"));
    /* white space to leave to the right of "Call site" */
    callsite_rspace = callsite_len - strlen("Call site");

    qemu_printf("Type               Object  Call site%*s  Wait Time (s)  "
                "       Count  Average (us)\n", callsite_rspace, "");

    /* build a horizontal rule with dashes */
    n_dashes = 79 + callsite_rspace;
    dashes = g_malloc(n_dashes + 1);
    memset(dashes, '-', n_dashes);
    dashes[n_dashes] = '\0';
    qemu_printf("%s\n", dashes);

    for (i = 0; i < rep->n_entries; i++) {
        const QSPReportEntry *e = &rep->entries[i];
        GString *s = g_string_new(NULL);

        g_string_append_printf(s, "%-9s  ", e->typename);
        if (e->n_objs > 1) {
            g_string_append_printf(s, "[%12u]", e->n_objs);
        } else {
            g_string_append_printf(s, "%14p", e->obj);
        }
        g_string_append_printf(s, "  %s%*s  %13.5f  %12" PRIu64 "  %12.2f\n",
                               e->callsite_at,
                               callsite_len - (int)strlen(e->callsite_at), "",
                               e->time_s, e->n_acqs, e->ns_avg * 1e-3);
        qemu_printf("%s", s->str);
        g_string_free(s, TRUE);
    }

    qemu_printf("%s\n", dashes);
    g_free(dashes);
}

static void report_destroy(QSPReport *rep)
{
    size_t i;

    for (i = 0; i < rep->n_entries; i++) {
        QSPReportEntry *e = &rep->entries[i];

        g_free(e->callsite_at);
    }
    g_free(rep->entries);
}

void qsp_report(size_t max, enum QSPSortBy sort_by,
                bool callsite_coalesce)
{
    GTree *tree = g_tree_new_full(qsp_tree_cmp, &sort_by, g_free, NULL);
    QSPReport rep;

    qsp_init();

    rep.entries = g_new0(QSPReportEntry, max);
    rep.n_entries = 0;
    rep.max_n_entries = max;

    qsp_mktree(tree, callsite_coalesce);
    g_tree_foreach(tree, qsp_tree_report, &rep);
    g_tree_destroy(tree);

    pr_report(&rep);
    report_destroy(&rep);
}

static void qsp_snapshot_destroy(QSPSnapshot *snap)
{
    qht_iter(&snap->ht, qsp_ht_delete, NULL);
    qht_destroy(&snap->ht);
    g_free(snap);
}

void qsp_reset(void)
{
    QSPSnapshot *new = g_new(QSPSnapshot, 1);
    QSPSnapshot *old;

    qsp_init();

    qht_init(&new->ht, qsp_entry_cmp, QSP_INITIAL_SIZE,
             QHT_MODE_AUTO_RESIZE | QHT_MODE_RAW_MUTEXES);

    /* take a snapshot of the current state */
    qht_iter(&qsp_ht, qsp_aggregate, &new->ht);

    /* replace the previous snapshot, if any */
    old = qatomic_xchg(&qsp_snapshot, new);
    if (old) {
        call_rcu(old, qsp_snapshot_destroy, rcu);
    }
}
