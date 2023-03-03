/*
 * gdbstub internals
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GDBSTUB_INTERNALS_H
#define GDBSTUB_INTERNALS_H

#include "exec/cpu-common.h"

#define MAX_PACKET_LENGTH 4096

/*
 * Shared structures and definitions
 */

enum {
    GDB_SIGNAL_0 = 0,
    GDB_SIGNAL_INT = 2,
    GDB_SIGNAL_QUIT = 3,
    GDB_SIGNAL_TRAP = 5,
    GDB_SIGNAL_ABRT = 6,
    GDB_SIGNAL_ALRM = 14,
    GDB_SIGNAL_IO = 23,
    GDB_SIGNAL_XCPU = 24,
    GDB_SIGNAL_UNKNOWN = 143
};

typedef struct GDBProcess {
    uint32_t pid;
    bool attached;

    char target_xml[1024];
} GDBProcess;

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_GETLINE_ESC,
    RS_GETLINE_RLE,
    RS_CHKSUM1,
    RS_CHKSUM2,
};

typedef struct GDBState {
    bool init;       /* have we been initialised? */
    CPUState *c_cpu; /* current CPU for step/continue ops */
    CPUState *g_cpu; /* current CPU for other ops */
    CPUState *query_cpu; /* for q{f|s}ThreadInfo */
    enum RSState state; /* parsing state */
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum; /* running checksum */
    int line_csum; /* checksum at the end of the packet */
    GByteArray *last_packet;
    int signal;
    bool multiprocess;
    GDBProcess *processes;
    int process_num;
    char syscall_buf[256];
    gdb_syscall_complete_cb current_syscall_cb;
    GString *str_buf;
    GByteArray *mem_buf;
    int sstep_flags;
    int supported_sstep_flags;
} GDBState;

/* lives in main gdbstub.c */
extern GDBState gdbserver_state;

/*
 * Inline utility function, convert from int to hex and back
 */

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9') {
        return v - '0';
    } else if (v >= 'A' && v <= 'F') {
        return v - 'A' + 10;
    } else if (v >= 'a' && v <= 'f') {
        return v - 'a' + 10;
    } else {
        return 0;
    }
}

static inline int tohex(int v)
{
    if (v < 10) {
        return v + '0';
    } else {
        return v - 10 + 'a';
    }
}

/*
 * Connection helpers for both softmmu and user backends
 */

void gdb_put_strbuf(void);
int gdb_put_packet(const char *buf);
int gdb_put_packet_binary(const char *buf, int len, bool dump);
void gdb_hextomem(GByteArray *mem, const char *buf, int len);
void gdb_memtohex(GString *buf, const uint8_t *mem, int len);
void gdb_memtox(GString *buf, const char *mem, int len);
void gdb_read_byte(uint8_t ch);

/* utility helpers */
CPUState *gdb_first_attached_cpu(void);
void gdb_append_thread_id(CPUState *cpu, GString *buf);
int gdb_get_cpu_index(CPUState *cpu);

void gdb_create_default_process(GDBState *s);

/*
 * Helpers with separate softmmu and user implementations
 */
void gdb_put_buffer(const uint8_t *buf, int len);

/*
 * Command handlers - either softmmu or user only
 */
void gdb_init_gdbserver_state(void);

typedef enum GDBThreadIdKind {
    GDB_ONE_THREAD = 0,
    GDB_ALL_THREADS,     /* One process, all threads */
    GDB_ALL_PROCESSES,
    GDB_READ_THREAD_ERR
} GDBThreadIdKind;

typedef union GdbCmdVariant {
    const char *data;
    uint8_t opcode;
    unsigned long val_ul;
    unsigned long long val_ull;
    struct {
        GDBThreadIdKind kind;
        uint32_t pid;
        uint32_t tid;
    } thread_id;
} GdbCmdVariant;

#define get_param(p, i)    (&g_array_index(p, GdbCmdVariant, i))

void gdb_handle_query_rcmd(GArray *params, void *user_ctx); /* softmmu */

/*
 * Break/Watch point support - there is an implementation for softmmu
 * and user mode.
 */
bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, vaddr addr, vaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, vaddr addr, vaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

#endif /* GDBSTUB_INTERNALS_H */
