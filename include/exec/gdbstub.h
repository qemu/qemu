#ifndef GDBSTUB_H
#define GDBSTUB_H

#define DEFAULT_GDBSTUB_PORT "1234"

/* GDB breakpoint/watchpoint types */
#define GDB_BREAKPOINT_SW        0
#define GDB_BREAKPOINT_HW        1
#define GDB_WATCHPOINT_WRITE     2
#define GDB_WATCHPOINT_READ      3
#define GDB_WATCHPOINT_ACCESS    4

#ifdef NEED_CPU_H
#include "cpu.h"

typedef void (*gdb_syscall_complete_cb)(CPUState *cpu,
                                        target_ulong ret, target_ulong err);

/**
 * gdb_do_syscall:
 * @cb: function to call when the system call has completed
 * @fmt: gdb syscall format string
 * ...: list of arguments to interpolate into @fmt
 *
 * Send a GDB syscall request. This function will return immediately;
 * the callback function will be called later when the remote system
 * call has completed.
 *
 * @fmt should be in the 'call-id,parameter,parameter...' format documented
 * for the F request packet in the GDB remote protocol. A limited set of
 * printf-style format specifiers is supported:
 *   %x  - target_ulong argument printed in hex
 *   %lx - 64-bit argument printed in hex
 *   %s  - string pointer (target_ulong) and length (int) pair
 */
void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...);
/**
 * gdb_do_syscallv:
 * @cb: function to call when the system call has completed
 * @fmt: gdb syscall format string
 * @va: arguments to interpolate into @fmt
 *
 * As gdb_do_syscall, but taking a va_list rather than a variable
 * argument list.
 */
void gdb_do_syscallv(gdb_syscall_complete_cb cb, const char *fmt, va_list va);
int use_gdb_syscalls(void);
void gdb_set_stop_cpu(CPUState *cpu);
void gdb_exit(CPUArchState *, int);
#ifdef CONFIG_USER_ONLY
/**
 * gdb_handlesig: yield control to gdb
 * @cpu: CPU
 * @sig: if non-zero, the signal number which caused us to stop
 *
 * This function yields control to gdb, when a user-mode-only target
 * needs to stop execution. If @sig is non-zero, then we will send a
 * stop packet to tell gdb that we have stopped because of this signal.
 *
 * This function will block (handling protocol requests from gdb)
 * until gdb tells us to continue target execution. When it does
 * return, the return value is a signal to deliver to the target,
 * or 0 if no signal should be delivered, ie the signal that caused
 * us to stop should be ignored.
 */
int gdb_handlesig(CPUState *, int);
void gdb_signalled(CPUArchState *, int);
void gdbserver_fork(CPUState *);
#endif
/* Get or set a register.  Returns the size of the register.  */
typedef int (*gdb_get_reg_cb)(CPUArchState *env, GByteArray *buf, int reg);
typedef int (*gdb_set_reg_cb)(CPUArchState *env, uint8_t *buf, int reg);
void gdb_register_coprocessor(CPUState *cpu,
                              gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
                              int num_regs, const char *xml, int g_pos);

/*
 * The GDB remote protocol transfers values in target byte order. As
 * the gdbstub may be batching up several register values we always
 * append to the array.
 */

static inline int gdb_get_reg8(GByteArray *buf, uint8_t val)
{
    g_byte_array_append(buf, &val, 1);
    return 1;
}

static inline int gdb_get_reg16(GByteArray *buf, uint16_t val)
{
    uint16_t to_word = tswap16(val);
    g_byte_array_append(buf, (uint8_t *) &to_word, 2);
    return 2;
}

static inline int gdb_get_reg32(GByteArray *buf, uint32_t val)
{
    uint32_t to_long = tswap32(val);
    g_byte_array_append(buf, (uint8_t *) &to_long, 4);
    return 4;
}

static inline int gdb_get_reg64(GByteArray *buf, uint64_t val)
{
    uint64_t to_quad = tswap64(val);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
    return 8;
}

static inline int gdb_get_reg128(GByteArray *buf, uint64_t val_hi,
                                 uint64_t val_lo)
{
    uint64_t to_quad;
#ifdef TARGET_WORDS_BIGENDIAN
    to_quad = tswap64(val_hi);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
    to_quad = tswap64(val_lo);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
#else
    to_quad = tswap64(val_lo);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
    to_quad = tswap64(val_hi);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
#endif
    return 16;
}

/**
 * gdb_get_reg_ptr: get pointer to start of last element
 * @len: length of element
 *
 * This is a helper function to extract the pointer to the last
 * element for additional processing. Some front-ends do additional
 * dynamic swapping of the elements based on CPU state.
 */
static inline uint8_t * gdb_get_reg_ptr(GByteArray *buf, int len)
{
    return buf->data + buf->len - len;
}

#if TARGET_LONG_BITS == 64
#define gdb_get_regl(buf, val) gdb_get_reg64(buf, val)
#define ldtul_p(addr) ldq_p(addr)
#else
#define gdb_get_regl(buf, val) gdb_get_reg32(buf, val)
#define ldtul_p(addr) ldl_p(addr)
#endif

#endif

#ifdef CONFIG_USER_ONLY
int gdbserver_start(int);
#else
int gdbserver_start(const char *port);
#endif

void gdbserver_cleanup(void);

/**
 * gdb_has_xml:
 * This is an ugly hack to cope with both new and old gdb.
 * If gdb sends qXfer:features:read then assume we're talking to a newish
 * gdb that understands target descriptions.
 */
extern bool gdb_has_xml;

/* in gdbstub-xml.c, generated by scripts/feature_to_c.sh */
extern const char *const xml_builtin[][2];

#endif
