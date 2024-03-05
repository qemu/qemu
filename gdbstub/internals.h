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
    GDB_SIGNAL_STOP = 17,
    GDB_SIGNAL_IO = 23,
    GDB_SIGNAL_XCPU = 24,
    GDB_SIGNAL_UNKNOWN = 143
};

typedef struct GDBProcess {
    uint32_t pid;
    bool attached;
    char *target_xml;
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
    GString *str_buf;
    GByteArray *mem_buf;
    int sstep_flags;
    int supported_sstep_flags;
    /*
     * Whether we are allowed to send a stop reply packet at this moment.
     * Must be set off after sending the stop reply itself.
     */
    bool allow_stop_reply;
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
 * Connection helpers for both system and user backends
 */

void gdb_put_strbuf(void);
int gdb_put_packet(const char *buf);
int gdb_put_packet_binary(const char *buf, int len, bool dump);
void gdb_hextomem(GByteArray *mem, const char *buf, int len);
void gdb_memtohex(GString *buf, const uint8_t *mem, int len);
void gdb_memtox(GString *buf, const char *mem, int len);
void gdb_read_byte(uint8_t ch);

/*
 * Packet acknowledgement - we handle this slightly differently
 * between user and softmmu mode, mainly to deal with the differences
 * between the flexible chardev and the direct fd approaches.
 *
 * We currently don't support a negotiated QStartNoAckMode
 */

/**
 * gdb_got_immediate_ack() - check ok to continue
 *
 * Returns true to continue, false to re-transmit for user only, the
 * softmmu stub always returns true.
 */
bool gdb_got_immediate_ack(void);
/* utility helpers */
GDBProcess *gdb_get_process(uint32_t pid);
CPUState *gdb_get_first_cpu_in_process(GDBProcess *process);
CPUState *gdb_first_attached_cpu(void);
void gdb_append_thread_id(CPUState *cpu, GString *buf);
int gdb_get_cpu_index(CPUState *cpu);
unsigned int gdb_get_max_cpus(void); /* both */
bool gdb_can_reverse(void); /* softmmu, stub for user */
int gdb_target_sigtrap(void); /* user */

void gdb_create_default_process(GDBState *s);

/* signal mapping, common for softmmu, specialised for user-mode */
int gdb_signal_to_target(int sig);
int gdb_target_signal_to_gdb(int sig);

int gdb_get_char(void); /* user only */

/**
 * gdb_continue() - handle continue in mode specific way.
 */
void gdb_continue(void);

/**
 * gdb_continue_partial() - handle partial continue in mode specific way.
 */
int gdb_continue_partial(char *newstates);

/*
 * Helpers with separate softmmu and user implementations
 */
void gdb_put_buffer(const uint8_t *buf, int len);

/*
 * Command handlers - either specialised or softmmu or user only
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
void gdb_handle_query_offsets(GArray *params, void *user_ctx); /* user */
void gdb_handle_query_xfer_auxv(GArray *params, void *user_ctx); /*user */
void gdb_handle_v_file_open(GArray *params, void *user_ctx); /* user */
void gdb_handle_v_file_close(GArray *params, void *user_ctx); /* user */
void gdb_handle_v_file_pread(GArray *params, void *user_ctx); /* user */
void gdb_handle_v_file_readlink(GArray *params, void *user_ctx); /* user */
void gdb_handle_query_xfer_exec_file(GArray *params, void *user_ctx); /* user */
void gdb_handle_set_catch_syscalls(GArray *params, void *user_ctx); /* user */
void gdb_handle_query_supported_user(const char *gdb_supported); /* user */
bool gdb_handle_set_thread_user(uint32_t pid, uint32_t tid); /* user */
bool gdb_handle_detach_user(uint32_t pid); /* user */

void gdb_handle_query_attached(GArray *params, void *user_ctx); /* both */

/* softmmu only */
void gdb_handle_query_qemu_phy_mem_mode(GArray *params, void *user_ctx);
void gdb_handle_set_qemu_phy_mem_mode(GArray *params, void *user_ctx);

/* sycall handling */
void gdb_handle_file_io(GArray *params, void *user_ctx);
bool gdb_handled_syscall(void);
void gdb_disable_syscalls(void);
void gdb_syscall_reset(void);

/* user/softmmu specific syscall handling */
void gdb_syscall_handling(const char *syscall_packet);

/*
 * Break/Watch point support - there is an implementation for softmmu
 * and user mode.
 */
bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, vaddr addr, vaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, vaddr addr, vaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

/**
 * gdb_target_memory_rw_debug() - handle debug access to memory
 * @cs: CPUState
 * @addr: nominal address, could be an entire physical address
 * @buf: data
 * @len: length of access
 * @is_write: is it a write operation
 *
 * This function is specialised depending on the mode we are running
 * in. For system guests we can switch the interpretation of the
 * address to a physical address.
 */
int gdb_target_memory_rw_debug(CPUState *cs, hwaddr addr,
                               uint8_t *buf, int len, bool is_write);

#endif /* GDBSTUB_INTERNALS_H */
