/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this module implements debugging functions for
 *  the Fast Virtual Disk (FVD) format.
 *============================================================================*/

#ifndef ENABLE_TRACE_IO
# define TRACE_REQUEST(...) do {} while (0)
# define TRACE_STORE_IN_FVD(...) do {} while (0)

#else
/* Monitor IO on a specific sector that triggers bugs. */
static inline void debug_sector (int64_t sector_num)
{
    if (FALSE) {
        if (sector_num == ((int64_t) 1023990LL)) {
            QPAUSE ("right sector");
        }
    }
}

static void TRACE_REQUEST (int do_write, int64_t sector_num, int nb_sectors)
{
    if (do_write) {
        QDEBUG ("TRACE_REQUEST: write sector_num=%" PRId64
                " nb_sectors=%d\n    [ ", sector_num, nb_sectors);
    } else {
        QDEBUG ("TRACE_REQUEST: read  sector_num=%" PRId64 " nb_sectors=%d\n"
                "[ ", sector_num, nb_sectors);
    }

    int64_t end = sector_num + nb_sectors;
    int64_t sec;
    for (sec = sector_num; sec < end; sec++) {
        QDEBUG ("sec%" PRId64 " ", sec);
        debug_sector (sec);
    }
    QDEBUG (" ]\n");
}

static void TRACE_STORE_IN_FVD (const char *str, int64_t sector_num,
                                int nb_sectors)
{
    QDEBUG ("TRACE_STORE: %s sector_num=%" PRId64 " nb_sectors=%d\n    [ ",
            str, sector_num, nb_sectors);
    int64_t end = sector_num + nb_sectors;
    int64_t sec;
    for (sec = sector_num; sec < end; sec++) {
        QDEBUG ("sec%" PRId64 " ", sec);
        debug_sector (sec);
    }
    QDEBUG (" ]\n");
}
#endif

#ifndef FVD_DEBUG
# define my_qemu_malloc g_malloc
# define my_qemu_mallocz g_malloc0
# define my_qemu_blockalign qemu_blockalign
# define my_qemu_free g_free
# define my_qemu_vfree qemu_vfree
# define my_qemu_aio_get qemu_aio_get
# define my_qemu_aio_release qemu_aio_release
# define COPY_UUID(to,from) do {} while (0)

#else
FILE *__fvd_debug_fp;
static unsigned long long int fvd_uuid = 1;
static int64_t pending_qemu_malloc = 0;
static int64_t pending_qemu_aio_get = 0;
static int64_t pending_local_writes = 0;
static const char *alloc_file;
static int alloc_line;

#define my_qemu_malloc(size) \
    ((void*)(alloc_file=__FILE__, alloc_line=__LINE__, _my_qemu_malloc(size)))

#define my_qemu_mallocz(size) \
    ((void*)(alloc_file=__FILE__, alloc_line=__LINE__, _my_qemu_mallocz(size)))

#define my_qemu_blockalign(bs,size) \
    ((void*)(alloc_file=__FILE__, \
             alloc_line=__LINE__, \
             _my_qemu_blockalign(bs,size)))

#define my_qemu_aio_get(pool,bs,cb,op) \
    ((void*)(alloc_file=__FILE__, \
             alloc_line=__LINE__, \
             _my_qemu_aio_get(pool,bs,cb,op)))

#define my_qemu_free(p) \
    (alloc_file=__FILE__, alloc_line=__LINE__, _my_qemu_free(p))

#define my_qemu_vfree(p) \
    (alloc_file=__FILE__, alloc_line=__LINE__, _my_qemu_vfree(p))

static void COPY_UUID (FvdAIOCB * to, FvdAIOCB * from)
{
    if (from) {
        to->uuid = from->uuid;
        FVD_DEBUG_ACB (to);
    }
}

#ifdef DEBUG_MEMORY_LEAK
# define MAX_TRACER 10485760
static int alloc_tracer_used = 1;        /* slot 0 is not used. */
static void **alloc_tracers = NULL;

static void __attribute__ ((constructor)) init_mem_alloc_tracers (void)
{
    if (!alloc_tracers) {
        alloc_tracers = g_malloc0(sizeof(void *) * MAX_TRACER);
    }
}

static void trace_alloc (void *p, size_t size)
{
    alloc_tracer_t *t = p;
    t->magic = FVD_ALLOC_MAGIC;
    t->alloc_file = alloc_file;
    t->alloc_line = alloc_line;
    t->size = size;

    if (alloc_tracer_used < MAX_TRACER) {
        t->alloc_tracer = alloc_tracer_used++;
        alloc_tracers[t->alloc_tracer] = t;
        QDEBUG ("Allocate memory using tracer%d in %s on line %d.\n",
                t->alloc_tracer, alloc_file, alloc_line);
    } else {
        t->alloc_tracer = 0;
    }

    /* Set header and footer to detect out-of-range writes. */
    if (size != (size_t) - 1) {
        uint8_t *q = (uint8_t *) p;
        uint64_t *header = (uint64_t *) (q + 512 - sizeof (uint64_t));
        uint64_t *footer = (uint64_t *) (q + size - 512);
        *header = FVD_ALLOC_MAGIC;
        *footer = FVD_ALLOC_MAGIC;
    }
}

static void trace_free (void *p)
{
    alloc_tracer_t *t = p;

    QDEBUG ("Free memory with tracer%d in %s on line %d.\n",
            t->alloc_tracer, alloc_file, alloc_line);
    ASSERT (t->magic == FVD_ALLOC_MAGIC && t->alloc_tracer >= 0);

    /* Check header and footer to detect out-of-range writes. */
    if (t->size != (size_t) - 1) {
        uint8_t *q = (uint8_t *) p;
        uint64_t *header = (uint64_t *) (q + 512 - sizeof (uint64_t));
        uint64_t *footer = (uint64_t *) (q + t->size - 512);
        ASSERT (*header == FVD_ALLOC_MAGIC);
        ASSERT (*footer == FVD_ALLOC_MAGIC);
    }

    if (t->alloc_tracer) {
        ASSERT (alloc_tracers[t->alloc_tracer] == t);
        alloc_tracers[t->alloc_tracer] = NULL;
        t->alloc_tracer = -INT_MAX;
    } else {
        t->alloc_tracer *= -1;        /* Guard against double free. */
    }
}

static void dump_alloc_tracers (void)
{
    int unfreed = 0;
    int i;
    for (i = 1; i < alloc_tracer_used; i++) {
        if (!alloc_tracers[i]) {
            continue;
        }

        unfreed++;
        alloc_tracer_t *t = alloc_tracers[i];

        if (t->size == (size_t) - 1) {
            FvdAIOCB *acb = container_of (alloc_tracers[i], FvdAIOCB, tracer);
            ASSERT (acb->magic == FVDAIOCB_MAGIC);
            QDEBUG ("Memory %p with tracer%d allocated in %s on line %d "
                    "(FvdAIOCB acb%llu-%p) is not freed. magic %s\n",
                    alloc_tracers[i], i, t->alloc_file, t->alloc_line,
                    acb->uuid, acb,
                    t->magic == FVD_ALLOC_MAGIC ? "correct" : "wrong");
        } else {
            QDEBUG ("Memory %p with tracer%d allocated in %s on line %d is "
                    "not freed. magic %s\n",
                    alloc_tracers[i], i, t->alloc_file, t->alloc_line,
                    t->magic == FVD_ALLOC_MAGIC ? "correct" : "wrong");

            uint8_t *q = (uint8_t *) t;
            uint64_t *header = (uint64_t *) (q + 512 - sizeof (uint64_t));
            uint64_t *footer = (uint64_t *) (q + t->size - 512);
            ASSERT (*header == FVD_ALLOC_MAGIC);
            ASSERT (*footer == FVD_ALLOC_MAGIC);
        }
    }

    QDEBUG ("Unfreed memory allocations: %d\n", unfreed);
}
#endif

static inline void *_my_qemu_aio_get (AIOCBInfo *pool, BlockDriverState *bs,
                                      BlockDriverCompletionFunc * cb,
                                      void *opaque)
{
    pending_qemu_aio_get++;
    FvdAIOCB *acb = (FvdAIOCB *) qemu_aio_get (&fvd_aio_pool, bs, cb, opaque);
    acb->uuid = ++fvd_uuid;
    acb->magic = FVDAIOCB_MAGIC;

    FVD_DEBUG_ACB (acb);

#ifdef DEBUG_MEMORY_LEAK
    trace_alloc (&acb->tracer, -1);
#endif

    return acb;
}

static inline void my_qemu_aio_release (void *p)
{
    pending_qemu_aio_get--;
    ASSERT (pending_qemu_aio_get >= 0);

#ifdef DEBUG_MEMORY_LEAK
    FvdAIOCB *acb = p;
    trace_free (&acb->tracer);
#endif

    qemu_aio_release (p);
}

static inline void *_my_qemu_malloc (size_t size)
{
    ASSERT (size > 0);
    pending_qemu_malloc++;
#ifndef DEBUG_MEMORY_LEAK
    return g_malloc(size);
#else

    size += 1024;        /* 512 bytes header and 512 bytes footer. */
    uint8_t *ret = g_malloc(size);
    trace_alloc (ret, size);
    return ret + 512;
#endif
}

static inline void *_my_qemu_mallocz (size_t size)
{
    ASSERT (size > 0);
    pending_qemu_malloc++;
#ifndef DEBUG_MEMORY_LEAK
    return g_malloc0(size);
#else

    size += 1024;        /* 512 bytes header and 512 bytes footer. */
    uint8_t *ret = g_malloc0(size);
    trace_alloc (ret, size);
    return ret + 512;
#endif
}

static inline void *_my_qemu_blockalign (BlockDriverState * bs, size_t size)
{
    ASSERT (size > 0);
    pending_qemu_malloc++;

#ifndef DEBUG_MEMORY_LEAK
    return qemu_blockalign (bs, size);
#else

    size += 1024;        /* 512 bytes header and 512 bytes footer. */
    uint8_t *ret = qemu_blockalign (bs, size);
    trace_alloc (ret, size);
    return ret + 512;
#endif
}

static inline void _my_qemu_free (void *ptr)
{
    pending_qemu_malloc--;
    ASSERT (pending_qemu_malloc >= 0);
#ifndef DEBUG_MEMORY_LEAK
    g_free(ptr);
#else

    uint8_t *q = ((uint8_t *) ptr) - 512;
    trace_free (q);
    g_free(q);
#endif
}

static inline void _my_qemu_vfree (void *ptr)
{
    pending_qemu_malloc--;
    ASSERT (pending_qemu_malloc >= 0);
#ifndef DEBUG_MEMORY_LEAK
    qemu_vfree (ptr);
#else

    uint8_t *q = ((uint8_t *) ptr) - 512;
    trace_free (q);
    qemu_vfree (q);
#endif
}

static void count_pending_requests (BDRVFvdState * s)
{
    int m = 0, k = 0;
    FvdAIOCB *w;

    QLIST_FOREACH (w, &s->copy_locks, copy_lock.next) {
        m++;
        QDEBUG ("copy_lock: acb%llu-%p\n", w->uuid, w);
    }

    QLIST_FOREACH (w, &s->write_locks, write.next_write_lock) {
        k++;
        QDEBUG ("write_lock: acb%llu-%p\n", w->uuid, w);
    }

    QDEBUG ("Debug_memory_leak: copy_locks=%d  write_locks=%d\n", m, k);
}

static void dump_resource_summary (BDRVFvdState * s)
{
#ifdef DEBUG_MEMORY_LEAK
    dump_alloc_tracers ();
#endif

    QDEBUG ("Resource summary: outstanding_copy_on_read_data=%" PRId64
            " total_copy_on_read_data=%" PRId64 " total_prefetch_data=%" PRId64
            " " " pending_qemu_malloc=%" PRId64 " pending_qemu_aio_get=%" PRId64
            " pending_local_writes=%" PRId64 "\n",
            s->outstanding_copy_on_read_data, s->total_copy_on_read_data,
            s->total_prefetch_data, pending_qemu_malloc, pending_qemu_aio_get,
            pending_local_writes);
    count_pending_requests (s);
}

/* Monitor processing a specific FvdAIOCB that triggers bugs. */
void FVD_DEBUG_ACB (void *p)
{
    if (FALSE) {
        FvdAIOCB *acb = p;

        /* Is it FvdAIOCB? */
        if (acb->magic != FVDAIOCB_MAGIC || acb->common.bs->drv != &bdrv_fvd) {
            /* Is it CompactChildCB? */
            CompactChildCB *child = p;
            acb = child->acb;
            if (acb->magic != FVDAIOCB_MAGIC
                || acb->common.bs->drv != &bdrv_fvd
                || (acb->type != OP_LOAD_COMPACT
                    && acb->type != OP_STORE_COMPACT)) {
                return;
            }
        }

        if (acb->uuid == 20ULL) {
            QPAUSE ("Processing the right acb");
        }
    }
}

void init_fvd_debug_fp (void)
{
    char buf[256];
    sprintf (buf, "/tmp/fvd.log-%d", getpid ());
    if ((__fvd_debug_fp = fopen (buf, "wt")) == NULL) {
        __fvd_debug_fp = stdout;
    }
}
#endif

void fvd_check_memory_usage (void)
{
    ASSERT (pending_qemu_malloc == 0);
}

int fvd_get_copy_on_read (BlockDriverState * bs)
{
    BDRVFvdState *s = bs->opaque;
    return s->copy_on_read;
}

void fvd_set_copy_on_read (BlockDriverState * bs, int copy_on_read)
{
    BDRVFvdState *s = bs->opaque;
    s->copy_on_read = copy_on_read;
}
