/*
 * Simple trace backend
 *
 * Copyright IBM, Corp. 2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#include <signal.h>
#include <pthread.h>
#endif
#include "qemu/timer.h"
#include "trace.h"
#include "trace/control.h"
#include "trace/simple.h"

/** Trace file header event ID */
#define HEADER_EVENT_ID (~(uint64_t)0) /* avoids conflicting with TraceEventIDs */

/** Trace file magic number */
#define HEADER_MAGIC 0xf2b177cb0aa429b4ULL

/** Trace file version number, bump if format changes */
#define HEADER_VERSION 3

/** Records were dropped event ID */
#define DROPPED_EVENT_ID (~(uint64_t)0 - 1)

/** Trace record is valid */
#define TRACE_RECORD_VALID ((uint64_t)1 << 63)

/*
 * Trace records are written out by a dedicated thread.  The thread waits for
 * records to become available, writes them out, and then waits again.
 */
#if GLIB_CHECK_VERSION(2, 32, 0)
static GMutex trace_lock;
#define lock_trace_lock() g_mutex_lock(&trace_lock)
#define unlock_trace_lock() g_mutex_unlock(&trace_lock)
#define get_trace_lock_mutex() (&trace_lock)
#else
static GStaticMutex trace_lock = G_STATIC_MUTEX_INIT;
#define lock_trace_lock() g_static_mutex_lock(&trace_lock)
#define unlock_trace_lock() g_static_mutex_unlock(&trace_lock)
#define get_trace_lock_mutex() g_static_mutex_get_mutex(&trace_lock)
#endif

/* g_cond_new() was deprecated in glib 2.31 but we still need to support it */
#if GLIB_CHECK_VERSION(2, 31, 0)
static GCond the_trace_available_cond;
static GCond the_trace_empty_cond;
static GCond *trace_available_cond = &the_trace_available_cond;
static GCond *trace_empty_cond = &the_trace_empty_cond;
#else
static GCond *trace_available_cond;
static GCond *trace_empty_cond;
#endif

static bool trace_available;
static bool trace_writeout_enabled;

enum {
    TRACE_BUF_LEN = 4096 * 64,
    TRACE_BUF_FLUSH_THRESHOLD = TRACE_BUF_LEN / 4,
};

uint8_t trace_buf[TRACE_BUF_LEN];
static volatile gint trace_idx;
static unsigned int writeout_idx;
static volatile gint dropped_events;
static FILE *trace_fp;
static char *trace_file_name;

/* * Trace buffer entry */
typedef struct {
    uint64_t event; /*   TraceEventID */
    uint64_t timestamp_ns;
    uint32_t length;   /*    in bytes */
    uint32_t reserved; /*    unused */
    uint64_t arguments[];
} TraceRecord;

typedef struct {
    uint64_t header_event_id; /* HEADER_EVENT_ID */
    uint64_t header_magic;    /* HEADER_MAGIC    */
    uint64_t header_version;  /* HEADER_VERSION  */
} TraceLogHeader;


static void read_from_buffer(unsigned int idx, void *dataptr, size_t size);
static unsigned int write_to_buffer(unsigned int idx, void *dataptr, size_t size);

static void clear_buffer_range(unsigned int idx, size_t len)
{
    uint32_t num = 0;
    while (num < len) {
        if (idx >= TRACE_BUF_LEN) {
            idx = idx % TRACE_BUF_LEN;
        }
        trace_buf[idx++] = 0;
        num++;
    }
}
/**
 * Read a trace record from the trace buffer
 *
 * @idx         Trace buffer index
 * @record      Trace record to fill
 *
 * Returns false if the record is not valid.
 */
static bool get_trace_record(unsigned int idx, TraceRecord **recordptr)
{
    uint64_t event_flag = 0;
    TraceRecord record;
    /* read the event flag to see if its a valid record */
    read_from_buffer(idx, &record, sizeof(event_flag));

    if (!(record.event & TRACE_RECORD_VALID)) {
        return false;
    }

    smp_rmb(); /* read memory barrier before accessing record */
    /* read the record header to know record length */
    read_from_buffer(idx, &record, sizeof(TraceRecord));
    *recordptr = malloc(record.length); /* dont use g_malloc, can deadlock when traced */
    /* make a copy of record to avoid being overwritten */
    read_from_buffer(idx, *recordptr, record.length);
    smp_rmb(); /* memory barrier before clearing valid flag */
    (*recordptr)->event &= ~TRACE_RECORD_VALID;
    /* clear the trace buffer range for consumed record otherwise any byte
     * with its MSB set may be considered as a valid event id when the writer
     * thread crosses this range of buffer again.
     */
    clear_buffer_range(idx, record.length);
    return true;
}

/**
 * Kick writeout thread
 *
 * @wait        Whether to wait for writeout thread to complete
 */
static void flush_trace_file(bool wait)
{
    lock_trace_lock();
    trace_available = true;
    g_cond_signal(trace_available_cond);

    if (wait) {
        g_cond_wait(trace_empty_cond, get_trace_lock_mutex());
    }

    unlock_trace_lock();
}

static void wait_for_trace_records_available(void)
{
    lock_trace_lock();
    while (!(trace_available && trace_writeout_enabled)) {
        g_cond_signal(trace_empty_cond);
        g_cond_wait(trace_available_cond, get_trace_lock_mutex());
    }
    trace_available = false;
    unlock_trace_lock();
}

static gpointer writeout_thread(gpointer opaque)
{
    TraceRecord *recordptr;
    union {
        TraceRecord rec;
        uint8_t bytes[sizeof(TraceRecord) + sizeof(uint64_t)];
    } dropped;
    unsigned int idx = 0;
    int dropped_count;
    size_t unused __attribute__ ((unused));

    for (;;) {
        wait_for_trace_records_available();

        if (g_atomic_int_get(&dropped_events)) {
            dropped.rec.event = DROPPED_EVENT_ID,
            dropped.rec.timestamp_ns = get_clock();
            dropped.rec.length = sizeof(TraceRecord) + sizeof(uint64_t),
            dropped.rec.reserved = 0;
            do {
                dropped_count = g_atomic_int_get(&dropped_events);
            } while (!g_atomic_int_compare_and_exchange(&dropped_events,
                                                        dropped_count, 0));
            dropped.rec.arguments[0] = dropped_count;
            unused = fwrite(&dropped.rec, dropped.rec.length, 1, trace_fp);
        }

        while (get_trace_record(idx, &recordptr)) {
            unused = fwrite(recordptr, recordptr->length, 1, trace_fp);
            writeout_idx += recordptr->length;
            free(recordptr); /* dont use g_free, can deadlock when traced */
            idx = writeout_idx % TRACE_BUF_LEN;
        }

        fflush(trace_fp);
    }
    return NULL;
}

void trace_record_write_u64(TraceBufferRecord *rec, uint64_t val)
{
    rec->rec_off = write_to_buffer(rec->rec_off, &val, sizeof(uint64_t));
}

void trace_record_write_str(TraceBufferRecord *rec, const char *s, uint32_t slen)
{
    /* Write string length first */
    rec->rec_off = write_to_buffer(rec->rec_off, &slen, sizeof(slen));
    /* Write actual string now */
    rec->rec_off = write_to_buffer(rec->rec_off, (void*)s, slen);
}

int trace_record_start(TraceBufferRecord *rec, TraceEventID event, size_t datasize)
{
    unsigned int idx, rec_off, old_idx, new_idx;
    uint32_t rec_len = sizeof(TraceRecord) + datasize;
    uint64_t event_u64 = event;
    uint64_t timestamp_ns = get_clock();

    do {
        old_idx = g_atomic_int_get(&trace_idx);
        smp_rmb();
        new_idx = old_idx + rec_len;

        if (new_idx - writeout_idx > TRACE_BUF_LEN) {
            /* Trace Buffer Full, Event dropped ! */
            g_atomic_int_inc(&dropped_events);
            return -ENOSPC;
        }
    } while (!g_atomic_int_compare_and_exchange(&trace_idx, old_idx, new_idx));

    idx = old_idx % TRACE_BUF_LEN;

    rec_off = idx;
    rec_off = write_to_buffer(rec_off, &event_u64, sizeof(event_u64));
    rec_off = write_to_buffer(rec_off, &timestamp_ns, sizeof(timestamp_ns));
    rec_off = write_to_buffer(rec_off, &rec_len, sizeof(rec_len));

    rec->tbuf_idx = idx;
    rec->rec_off  = (idx + sizeof(TraceRecord)) % TRACE_BUF_LEN;
    return 0;
}

static void read_from_buffer(unsigned int idx, void *dataptr, size_t size)
{
    uint8_t *data_ptr = dataptr;
    uint32_t x = 0;
    while (x < size) {
        if (idx >= TRACE_BUF_LEN) {
            idx = idx % TRACE_BUF_LEN;
        }
        data_ptr[x++] = trace_buf[idx++];
    }
}

static unsigned int write_to_buffer(unsigned int idx, void *dataptr, size_t size)
{
    uint8_t *data_ptr = dataptr;
    uint32_t x = 0;
    while (x < size) {
        if (idx >= TRACE_BUF_LEN) {
            idx = idx % TRACE_BUF_LEN;
        }
        trace_buf[idx++] = data_ptr[x++];
    }
    return idx; /* most callers wants to know where to write next */
}

void trace_record_finish(TraceBufferRecord *rec)
{
    TraceRecord record;
    read_from_buffer(rec->tbuf_idx, &record, sizeof(TraceRecord));
    smp_wmb(); /* write barrier before marking as valid */
    record.event |= TRACE_RECORD_VALID;
    write_to_buffer(rec->tbuf_idx, &record, sizeof(TraceRecord));

    if (((unsigned int)g_atomic_int_get(&trace_idx) - writeout_idx)
        > TRACE_BUF_FLUSH_THRESHOLD) {
        flush_trace_file(false);
    }
}

void st_set_trace_file_enabled(bool enable)
{
    if (enable == !!trace_fp) {
        return; /* no change */
    }

    /* Halt trace writeout */
    flush_trace_file(true);
    trace_writeout_enabled = false;
    flush_trace_file(true);

    if (enable) {
        static const TraceLogHeader header = {
            .header_event_id = HEADER_EVENT_ID,
            .header_magic = HEADER_MAGIC,
            /* Older log readers will check for version at next location */
            .header_version = HEADER_VERSION,
        };

        trace_fp = fopen(trace_file_name, "wb");
        if (!trace_fp) {
            return;
        }

        if (fwrite(&header, sizeof header, 1, trace_fp) != 1) {
            fclose(trace_fp);
            trace_fp = NULL;
            return;
        }

        /* Resume trace writeout */
        trace_writeout_enabled = true;
        flush_trace_file(false);
    } else {
        fclose(trace_fp);
        trace_fp = NULL;
    }
}

/**
 * Set the name of a trace file
 *
 * @file        The trace file name or NULL for the default name-<pid> set at
 *              config time
 */
bool st_set_trace_file(const char *file)
{
    st_set_trace_file_enabled(false);

    g_free(trace_file_name);

    if (!file) {
        trace_file_name = g_strdup_printf(CONFIG_TRACE_FILE, getpid());
    } else {
        trace_file_name = g_strdup_printf("%s", file);
    }

    st_set_trace_file_enabled(true);
    return true;
}

void st_print_trace_file_status(FILE *stream, int (*stream_printf)(FILE *stream, const char *fmt, ...))
{
    stream_printf(stream, "Trace file \"%s\" %s.\n",
                  trace_file_name, trace_fp ? "on" : "off");
}

void st_flush_trace_buffer(void)
{
    flush_trace_file(true);
}

void trace_print_events(FILE *stream, fprintf_function stream_printf)
{
    unsigned int i;

    for (i = 0; i < trace_event_count(); i++) {
        TraceEvent *ev = trace_event_id(i);
        stream_printf(stream, "%s [Event ID %u] : state %u\n",
                      trace_event_get_name(ev), i, trace_event_get_state_dynamic(ev));
    }
}

void trace_event_set_state_dynamic_backend(TraceEvent *ev, bool state)
{
    ev->dstate = state;
}

/* Helper function to create a thread with signals blocked.  Use glib's
 * portable threads since QEMU abstractions cannot be used due to reentrancy in
 * the tracer.  Also note the signal masking on POSIX hosts so that the thread
 * does not steal signals when the rest of the program wants them blocked.
 */
static GThread *trace_thread_create(GThreadFunc fn)
{
    GThread *thread;
#ifndef _WIN32
    sigset_t set, oldset;

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &oldset);
#endif

#if GLIB_CHECK_VERSION(2, 31, 0)
    thread = g_thread_new("trace-thread", fn, NULL);
#else
    thread = g_thread_create(fn, NULL, FALSE, NULL);
#endif

#ifndef _WIN32
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif

    return thread;
}

bool trace_backend_init(const char *events, const char *file)
{
    GThread *thread;

#if !GLIB_CHECK_VERSION(2, 31, 0)
    trace_available_cond = g_cond_new();
    trace_empty_cond = g_cond_new();
#endif

    thread = trace_thread_create(writeout_thread);
    if (!thread) {
        fprintf(stderr, "warning: unable to initialize simple trace backend\n");
        return false;
    }

    atexit(st_flush_trace_buffer);
    trace_backend_init_events(events);
    st_set_trace_file(file);
    return true;
}
