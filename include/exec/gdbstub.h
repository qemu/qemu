#ifndef GDBSTUB_H
#define GDBSTUB_H

#define DEFAULT_GDBSTUB_PORT "1234"

/* GDB breakpoint/watchpoint types */
#define GDB_BREAKPOINT_SW        0
#define GDB_BREAKPOINT_HW        1
#define GDB_WATCHPOINT_WRITE     2
#define GDB_WATCHPOINT_READ      3
#define GDB_WATCHPOINT_ACCESS    4

/* For gdb file i/o remote protocol open flags. */
#define GDB_O_RDONLY  0
#define GDB_O_WRONLY  1
#define GDB_O_RDWR    2
#define GDB_O_APPEND  8
#define GDB_O_CREAT   0x200
#define GDB_O_TRUNC   0x400
#define GDB_O_EXCL    0x800

/* For gdb file i/o remote protocol errno values */
#define GDB_EPERM           1
#define GDB_ENOENT          2
#define GDB_EINTR           4
#define GDB_EBADF           9
#define GDB_EACCES         13
#define GDB_EFAULT         14
#define GDB_EBUSY          16
#define GDB_EEXIST         17
#define GDB_ENODEV         19
#define GDB_ENOTDIR        20
#define GDB_EISDIR         21
#define GDB_EINVAL         22
#define GDB_ENFILE         23
#define GDB_EMFILE         24
#define GDB_EFBIG          27
#define GDB_ENOSPC         28
#define GDB_ESPIPE         29
#define GDB_EROFS          30
#define GDB_ENAMETOOLONG   91
#define GDB_EUNKNOWN       9999

/* For gdb file i/o remote protocol lseek whence. */
#define GDB_SEEK_SET  0
#define GDB_SEEK_CUR  1
#define GDB_SEEK_END  2

/* For gdb file i/o stat/fstat. */
typedef uint32_t gdb_mode_t;
typedef uint32_t gdb_time_t;

struct gdb_stat {
  uint32_t    gdb_st_dev;     /* device */
  uint32_t    gdb_st_ino;     /* inode */
  gdb_mode_t  gdb_st_mode;    /* protection */
  uint32_t    gdb_st_nlink;   /* number of hard links */
  uint32_t    gdb_st_uid;     /* user ID of owner */
  uint32_t    gdb_st_gid;     /* group ID of owner */
  uint32_t    gdb_st_rdev;    /* device type (if inode device) */
  uint64_t    gdb_st_size;    /* total size, in bytes */
  uint64_t    gdb_st_blksize; /* blocksize for filesystem I/O */
  uint64_t    gdb_st_blocks;  /* number of blocks allocated */
  gdb_time_t  gdb_st_atime;   /* time of last access */
  gdb_time_t  gdb_st_mtime;   /* time of last modification */
  gdb_time_t  gdb_st_ctime;   /* time of last change */
} QEMU_PACKED;

struct gdb_timeval {
  gdb_time_t tv_sec;  /* second */
  uint64_t tv_usec;   /* microsecond */
} QEMU_PACKED;

#ifdef NEED_CPU_H
#include "cpu.h"

typedef void (*gdb_syscall_complete_cb)(CPUState *cpu, uint64_t ret, int err);

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
#if TARGET_BIG_ENDIAN
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

static inline int gdb_get_zeroes(GByteArray *array, size_t len)
{
    guint oldlen = array->len;
    g_byte_array_set_size(array, oldlen + len);
    memset(array->data + oldlen, 0, len);

    return len;
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

#endif /* NEED_CPU_H */

/**
 * gdbserver_start: start the gdb server
 * @port_or_device: connection spec for gdb
 *
 * For CONFIG_USER this is either a tcp port or a path to a fifo. For
 * system emulation you can use a full chardev spec for your gdbserver
 * port.
 */
int gdbserver_start(const char *port_or_device);

/**
 * gdb_exit: exit gdb session, reporting inferior status
 * @code: exit code reported
 *
 * This closes the session and sends a final packet to GDB reporting
 * the exit status of the program. It also cleans up any connections
 * detritus before returning.
 */
void gdb_exit(int code);

void gdb_set_stop_cpu(CPUState *cpu);

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
