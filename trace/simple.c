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
#include <signal.h>
#include <pthread.h>
#include "qemu-timer.h"
#include "trace.h"
#include "trace/control.h"

/** Trace file header event ID */
#define HEADER_EVENT_ID (~(uint64_t)0) /* avoids conflicting with TraceEventIDs */

/** Trace file magic number */
#define HEADER_MAGIC 0xf2b177cb0aa429b4ULL

/** Trace file version number, bump if format changes */
#define HEADER_VERSION 0

/** Records were dropped event ID */
#define DROPPED_EVENT_ID (~(uint64_t)0 - 1)

/** Trace record is valid */
#define TRACE_RECORD_VALID ((uint64_t)1 << 63)

/** Trace buffer entry */
typedef struct {
    uint64_t event;
    uint64_t timestamp_ns;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
} TraceRecord;

enum {
    TRACE_BUF_LEN = 4096,
    TRACE_BUF_FLUSH_THRESHOLD = TRACE_BUF_LEN / 4,
};

/*
 * Trace records are written out by a dedicated thread.  The thread waits for
 * records to become available, writes them out, and then waits again.
 */
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t trace_available_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t trace_empty_cond = PTHREAD_COND_INITIALIZER;
static bool trace_available;
static bool trace_writeout_enabled;

static TraceRecord trace_buf[TRACE_BUF_LEN];
static unsigned int trace_idx;
static FILE *trace_fp;
static char *trace_file_name = NULL;

/**
 * Read a trace record from the trace buffer
 *
 * @idx         Trace buffer index
 * @record      Trace record to fill
 *
 * Returns false if the record is not valid.
 */
static bool get_trace_record(unsigned int idx, TraceRecord *record)
{
    if (!(trace_buf[idx].event & TRACE_RECORD_VALID)) {
        return false;
    }

    __sync_synchronize(); /* read memory barrier before accessing record */

    *record = trace_buf[idx];
    record->event &= ~TRACE_RECORD_VALID;
    return true;
}

/**
 * Kick writeout thread
 *
 * @wait        Whether to wait for writeout thread to complete
 */
static void flush_trace_file(bool wait)
{
    pthread_mutex_lock(&trace_lock);
    trace_available = true;
    pthread_cond_signal(&trace_available_cond);

    if (wait) {
        pthread_cond_wait(&trace_empty_cond, &trace_lock);
    }

    pthread_mutex_unlock(&trace_lock);
}

static void wait_for_trace_records_available(void)
{
    pthread_mutex_lock(&trace_lock);
    while (!(trace_available && trace_writeout_enabled)) {
        pthread_cond_signal(&trace_empty_cond);
        pthread_cond_wait(&trace_available_cond, &trace_lock);
    }
    trace_available = false;
    pthread_mutex_unlock(&trace_lock);
}

static void *writeout_thread(void *opaque)
{
    TraceRecord record;
    unsigned int writeout_idx = 0;
    unsigned int num_available, idx;
    size_t unused __attribute__ ((unused));

    for (;;) {
        wait_for_trace_records_available();

        num_available = trace_idx - writeout_idx;
        if (num_available > TRACE_BUF_LEN) {
            record = (TraceRecord){
                .event = DROPPED_EVENT_ID,
                .x1 = num_available,
            };
            unused = fwrite(&record, sizeof(record), 1, trace_fp);
            writeout_idx += num_available;
        }

        idx = writeout_idx % TRACE_BUF_LEN;
        while (get_trace_record(idx, &record)) {
            trace_buf[idx].event = 0; /* clear valid bit */
            unused = fwrite(&record, sizeof(record), 1, trace_fp);
            idx = ++writeout_idx % TRACE_BUF_LEN;
        }

        fflush(trace_fp);
    }
    return NULL;
}

static void trace(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3,
                  uint64_t x4, uint64_t x5, uint64_t x6)
{
    unsigned int idx;
    uint64_t timestamp;

    if (!trace_list[event].state) {
        return;
    }

    timestamp = get_clock();

    idx = __sync_fetch_and_add(&trace_idx, 1) % TRACE_BUF_LEN;
    trace_buf[idx] = (TraceRecord){
        .event = event,
        .timestamp_ns = timestamp,
        .x1 = x1,
        .x2 = x2,
        .x3 = x3,
        .x4 = x4,
        .x5 = x5,
        .x6 = x6,
    };
    __sync_synchronize(); /* write barrier before marking as valid */
    trace_buf[idx].event |= TRACE_RECORD_VALID;

    if ((idx + 1) % TRACE_BUF_FLUSH_THRESHOLD == 0) {
        flush_trace_file(false);
    }
}

void trace0(TraceEventID event)
{
    trace(event, 0, 0, 0, 0, 0, 0);
}

void trace1(TraceEventID event, uint64_t x1)
{
    trace(event, x1, 0, 0, 0, 0, 0);
}

void trace2(TraceEventID event, uint64_t x1, uint64_t x2)
{
    trace(event, x1, x2, 0, 0, 0, 0);
}

void trace3(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3)
{
    trace(event, x1, x2, x3, 0, 0, 0);
}

void trace4(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4)
{
    trace(event, x1, x2, x3, x4, 0, 0);
}

void trace5(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5)
{
    trace(event, x1, x2, x3, x4, x5, 0);
}

void trace6(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6)
{
    trace(event, x1, x2, x3, x4, x5, x6);
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
        static const TraceRecord header = {
            .event = HEADER_EVENT_ID,
            .timestamp_ns = HEADER_MAGIC,
            .x1 = HEADER_VERSION,
        };

        trace_fp = fopen(trace_file_name, "w");
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

    free(trace_file_name);

    if (!file) {
        if (asprintf(&trace_file_name, CONFIG_TRACE_FILE, getpid()) < 0) {
            trace_file_name = NULL;
            return false;
        }
    } else {
        if (asprintf(&trace_file_name, "%s", file) < 0) {
            trace_file_name = NULL;
            return false;
        }
    }

    st_set_trace_file_enabled(true);
    return true;
}

void st_print_trace_file_status(FILE *stream, int (*stream_printf)(FILE *stream, const char *fmt, ...))
{
    stream_printf(stream, "Trace file \"%s\" %s.\n",
                  trace_file_name, trace_fp ? "on" : "off");
}

void st_print_trace(FILE *stream, int (*stream_printf)(FILE *stream, const char *fmt, ...))
{
    unsigned int i;

    for (i = 0; i < TRACE_BUF_LEN; i++) {
        TraceRecord record;

        if (!get_trace_record(i, &record)) {
            continue;
        }
        stream_printf(stream, "Event %" PRIu64 " : %" PRIx64 " %" PRIx64
                      " %" PRIx64 " %" PRIx64 " %" PRIx64 " %" PRIx64 "\n",
                      record.event, record.x1, record.x2,
                      record.x3, record.x4, record.x5,
                      record.x6);
    }
}

void st_print_trace_events(FILE *stream, int (*stream_printf)(FILE *stream, const char *fmt, ...))
{
    unsigned int i;

    for (i = 0; i < NR_TRACE_EVENTS; i++) {
        stream_printf(stream, "%s [Event ID %u] : state %u\n",
                      trace_list[i].tp_name, i, trace_list[i].state);
    }
}

bool st_change_trace_event_state(const char *name, bool enabled)
{
    unsigned int i;

    for (i = 0; i < NR_TRACE_EVENTS; i++) {
        if (!strcmp(trace_list[i].tp_name, name)) {
            trace_list[i].state = enabled;
            return true;
        }
    }
    return false;
}

void st_flush_trace_buffer(void)
{
    flush_trace_file(true);
}

bool trace_backend_init(const char *file)
{
    pthread_t thread;
    pthread_attr_t attr;
    sigset_t set, oldset;
    int ret;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &oldset);
    ret = pthread_create(&thread, &attr, writeout_thread, NULL);
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    if (ret != 0) {
        fprintf(stderr, "warning: unable to initialize simple trace backend\n");
    } else {
        atexit(st_flush_trace_buffer);
        st_set_trace_file(file);
    }

    return true;
}
