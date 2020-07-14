/*
 * This file is supposed to be included only by .c files. No header file should
 * depend on qemu-common.h, as this would easily lead to circular header
 * dependencies.
 *
 * If a header file uses a definition from qemu-common.h, that definition
 * must be moved to a separate header file, and the header that uses it
 * must include that header.
 */
#ifndef QEMU_COMMON_H
#define QEMU_COMMON_H

#define TFR(expr) do { if ((expr) != -1) break; } while (errno == EINTR)

/* Copyright string for -version arguments, About dialogs, etc */
#define QEMU_COPYRIGHT "Copyright (c) 2003-2020 " \
    "Fabrice Bellard and the QEMU Project developers"

/* Bug reporting information for --help arguments, About dialogs, etc */
#define QEMU_HELP_BOTTOM \
    "See <https://qemu.org/contribute/report-a-bug> for how to report bugs.\n" \
    "More information on the QEMU project at <https://qemu.org>."

/* main function, renamed */
#if defined(CONFIG_COCOA)
int qemu_main(int argc, char **argv, char **envp);
#endif

void qemu_get_timedate(struct tm *tm, int offset);
int qemu_timedate_diff(struct tm *tm);

void *qemu_oom_check(void *ptr);

ssize_t qemu_write_full(int fd, const void *buf, size_t count)
    QEMU_WARN_UNUSED_RESULT;

#ifndef _WIN32
int qemu_pipe(int pipefd[2]);
/* like openpty() but also makes it raw; return master fd */
int qemu_openpty_raw(int *aslave, char *pty_name);
#endif

#ifdef _WIN32
/* MinGW needs type casts for the 'buf' and 'optval' arguments. */
#define qemu_getsockopt(sockfd, level, optname, optval, optlen) \
    getsockopt(sockfd, level, optname, (void *)optval, optlen)
#define qemu_setsockopt(sockfd, level, optname, optval, optlen) \
    setsockopt(sockfd, level, optname, (const void *)optval, optlen)
#define qemu_recv(sockfd, buf, len, flags) recv(sockfd, (void *)buf, len, flags)
#define qemu_sendto(sockfd, buf, len, flags, destaddr, addrlen) \
    sendto(sockfd, (const void *)buf, len, flags, destaddr, addrlen)
#else
#define qemu_getsockopt(sockfd, level, optname, optval, optlen) \
    getsockopt(sockfd, level, optname, optval, optlen)
#define qemu_setsockopt(sockfd, level, optname, optval, optlen) \
    setsockopt(sockfd, level, optname, optval, optlen)
#define qemu_recv(sockfd, buf, len, flags) recv(sockfd, buf, len, flags)
#define qemu_sendto(sockfd, buf, len, flags, destaddr, addrlen) \
    sendto(sockfd, buf, len, flags, destaddr, addrlen)
#endif

void cpu_exec_init_all(void);
void cpu_exec_step_atomic(CPUState *cpu);

/**
 * set_preferred_target_page_bits:
 * @bits: number of bits needed to represent an address within the page
 *
 * Set the preferred target page size (the actual target page
 * size may be smaller than any given CPU's preference).
 * Returns true on success, false on failure (which can only happen
 * if this is called after the system has already finalized its
 * choice of page size and the requested page size is smaller than that).
 */
bool set_preferred_target_page_bits(int bits);

/**
 * finalize_target_page_bits:
 * Commit the final value set by set_preferred_target_page_bits.
 */
void finalize_target_page_bits(void);

/**
 * Sends a (part of) iovec down a socket, yielding when the socket is full, or
 * Receives data into a (part of) iovec from a socket,
 * yielding when there is no data in the socket.
 * The same interface as qemu_sendv_recvv(), with added yielding.
 * XXX should mark these as coroutine_fn
 */
ssize_t qemu_co_sendv_recvv(int sockfd, struct iovec *iov, unsigned iov_cnt,
                            size_t offset, size_t bytes, bool do_send);
#define qemu_co_recvv(sockfd, iov, iov_cnt, offset, bytes) \
  qemu_co_sendv_recvv(sockfd, iov, iov_cnt, offset, bytes, false)
#define qemu_co_sendv(sockfd, iov, iov_cnt, offset, bytes) \
  qemu_co_sendv_recvv(sockfd, iov, iov_cnt, offset, bytes, true)

/**
 * The same as above, but with just a single buffer
 */
ssize_t qemu_co_send_recv(int sockfd, void *buf, size_t bytes, bool do_send);
#define qemu_co_recv(sockfd, buf, bytes) \
  qemu_co_send_recv(sockfd, buf, bytes, false)
#define qemu_co_send(sockfd, buf, bytes) \
  qemu_co_send_recv(sockfd, buf, bytes, true)

void qemu_progress_init(int enabled, float min_skip);
void qemu_progress_end(void);
void qemu_progress_print(float delta, int max);
const char *qemu_get_vm_name(void);

#define QEMU_FILE_TYPE_BIOS   0
#define QEMU_FILE_TYPE_KEYMAP 1
/**
 * qemu_find_file:
 * @type: QEMU_FILE_TYPE_BIOS (for BIOS, VGA BIOS)
 *        or QEMU_FILE_TYPE_KEYMAP (for keymaps).
 * @name: Relative or absolute file name
 *
 * If @name exists on disk as an absolute path, or a path relative
 * to the current directory, then returns @name unchanged.
 * Otherwise searches for @name file in the data directories, either
 * configured at build time (DATADIR) or registered with the -L command
 * line option.
 *
 * The caller must use g_free() to free the returned data when it is
 * no longer required.
 *
 * Returns: a path that can access @name, or NULL if no matching file exists.
 */
char *qemu_find_file(int type, const char *name);

/* OS specific functions */
void os_setup_early_signal_handling(void);
char *os_find_datadir(void);
int os_parse_cmd_args(int index, const char *optarg);

/*
 * Hexdump a buffer to a file. An optional string prefix is added to every line
 */

void qemu_hexdump(const char *buf, FILE *fp, const char *prefix, size_t size);

/*
 * helper to parse debug environment variables
 */
int parse_debug_env(const char *name, int max, int initial);

const char *qemu_ether_ntoa(const MACAddr *mac);
void page_size_init(void);

/* returns non-zero if dump is in progress, otherwise zero is
 * returned. */
bool dump_in_progress(void);

#endif
