/*
 * Simple trace backend
 *
 * Copyright IBM, Corp. 2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef TRACE_SIMPLE_H
#define TRACE_SIMPLE_H

void st_print_trace_file_status(void);
void st_set_trace_file_enabled(bool enable);
void st_set_trace_file(const char *file);
bool st_init(void);
void st_flush_trace_buffer(void);

typedef struct {
    unsigned int tbuf_idx;
    unsigned int rec_off;
} TraceBufferRecord;

/* Note for hackers: Make sure MAX_TRACE_LEN < sizeof(uint32_t) */
#define MAX_TRACE_STRLEN 512
/**
 * Initialize a trace record and claim space for it in the buffer
 *
 * @arglen  number of bytes required for arguments
 */
int trace_record_start(TraceBufferRecord *rec, uint32_t id, size_t arglen);

/**
 * Append a 64-bit argument to a trace record
 */
void trace_record_write_u64(TraceBufferRecord *rec, uint64_t val);

/**
 * Append a string argument to a trace record
 */
void trace_record_write_str(TraceBufferRecord *rec, const char *s, uint32_t slen);

/**
 * Mark a trace record completed
 *
 * Don't append any more arguments to the trace record after calling this.
 */
void trace_record_finish(TraceBufferRecord *rec);

#endif /* TRACE_SIMPLE_H */
