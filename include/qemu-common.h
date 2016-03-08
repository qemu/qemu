
/* Common header file that is included by all of QEMU.
 *
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

#include "qemu/typedefs.h"
#include "qemu/fprintf-fn.h"

#if defined(__arm__) || defined(__sparc__) || defined(__mips__) || defined(__hppa__) || defined(__ia64__)
#define WORDS_ALIGNED
#endif

#define TFR(expr) do { if ((expr) != -1) break; } while (errno == EINTR)

#include "qemu/option.h"
#include "qemu/host-utils.h"

/* HOST_LONG_BITS is the size of a native pointer in bits. */
#if UINTPTR_MAX == UINT32_MAX
# define HOST_LONG_BITS 32
#elif UINTPTR_MAX == UINT64_MAX
# define HOST_LONG_BITS 64
#else
# error Unknown pointer size
#endif

void cpu_ticks_init(void);

/* icount */
void configure_icount(QemuOpts *opts, Error **errp);
extern int use_icount;
extern int icount_align_option;
/* drift information for info jit command */
extern int64_t max_delay;
extern int64_t max_advance;
void dump_drift_info(FILE *f, fprintf_function cpu_fprintf);

#include "qemu/bswap.h"

/* FIXME: Remove NEED_CPU_H.  */
#ifdef NEED_CPU_H
#include "cpu.h"
#endif /* !defined(NEED_CPU_H) */

/* main function, renamed */
#if defined(CONFIG_COCOA)
int qemu_main(int argc, char **argv, char **envp);
#endif

void qemu_get_timedate(struct tm *tm, int offset);
int qemu_timedate_diff(struct tm *tm);

/**
 * is_help_option:
 * @s: string to test
 *
 * Check whether @s is one of the standard strings which indicate
 * that the user is asking for a list of the valid values for a
 * command option like -cpu or -M. The current accepted strings
 * are 'help' and '?'. '?' is deprecated (it is a shell wildcard
 * which makes it annoying to use in a reliable way) but provided
 * for backwards compatibility.
 *
 * Returns: true if @s is a request for a list.
 */
static inline bool is_help_option(const char *s)
{
    return !strcmp(s, "?") || !strcmp(s, "help");
}

/* util/cutils.c */
/**
 * pstrcpy:
 * @buf: buffer to copy string into
 * @buf_size: size of @buf in bytes
 * @str: string to copy
 *
 * Copy @str into @buf, including the trailing NUL, but do not
 * write more than @buf_size bytes. The resulting buffer is
 * always NUL terminated (even if the source string was too long).
 * If @buf_size is zero or negative then no bytes are copied.
 *
 * This function is similar to strncpy(), but avoids two of that
 * function's problems:
 *  * if @str fits in the buffer, pstrcpy() does not zero-fill the
 *    remaining space at the end of @buf
 *  * if @str is too long, pstrcpy() will copy the first @buf_size-1
 *    bytes and then add a NUL
 */
void pstrcpy(char *buf, int buf_size, const char *str);
/**
 * strpadcpy:
 * @buf: buffer to copy string into
 * @buf_size: size of @buf in bytes
 * @str: string to copy
 * @pad: character to pad the remainder of @buf with
 *
 * Copy @str into @buf (but *not* its trailing NUL!), and then pad the
 * rest of the buffer with the @pad character. If @str is too large
 * for the buffer then it is truncated, so that @buf contains the
 * first @buf_size characters of @str, with no terminator.
 */
void strpadcpy(char *buf, int buf_size, const char *str, char pad);
/**
 * pstrcat:
 * @buf: buffer containing existing string
 * @buf_size: size of @buf in bytes
 * @s: string to concatenate to @buf
 *
 * Append a copy of @s to the string already in @buf, but do not
 * allow the buffer to overflow. If the existing contents of @buf
 * plus @str would total more than @buf_size bytes, then write
 * as much of @str as will fit followed by a NUL terminator.
 *
 * @buf must already contain a NUL-terminated string, or the
 * behaviour is undefined.
 *
 * Returns: @buf.
 */
char *pstrcat(char *buf, int buf_size, const char *s);
/**
 * strstart:
 * @str: string to test
 * @val: prefix string to look for
 * @ptr: NULL, or pointer to be written to indicate start of
 *       the remainder of the string
 *
 * Test whether @str starts with the prefix @val.
 * If it does (including the degenerate case where @str and @val
 * are equal) then return true. If @ptr is not NULL then a
 * pointer to the first character following the prefix is written
 * to it. If @val is not a prefix of @str then return false (and
 * @ptr is not written to).
 *
 * Returns: true if @str starts with prefix @val, false otherwise.
 */
int strstart(const char *str, const char *val, const char **ptr);
/**
 * stristart:
 * @str: string to test
 * @val: prefix string to look for
 * @ptr: NULL, or pointer to be written to indicate start of
 *       the remainder of the string
 *
 * Test whether @str starts with the case-insensitive prefix @val.
 * This function behaves identically to strstart(), except that the
 * comparison is made after calling qemu_toupper() on each pair of
 * characters.
 *
 * Returns: true if @str starts with case-insensitive prefix @val,
 *          false otherwise.
 */
int stristart(const char *str, const char *val, const char **ptr);
/**
 * qemu_strnlen:
 * @s: string
 * @max_len: maximum number of bytes in @s to scan
 *
 * Return the length of the string @s, like strlen(), but do not
 * examine more than @max_len bytes of the memory pointed to by @s.
 * If no NUL terminator is found within @max_len bytes, then return
 * @max_len instead.
 *
 * This function has the same behaviour as the POSIX strnlen()
 * function.
 *
 * Returns: length of @s in bytes, or @max_len, whichever is smaller.
 */
int qemu_strnlen(const char *s, int max_len);
/**
 * qemu_strsep:
 * @input: pointer to string to parse
 * @delim: string containing delimiter characters to search for
 *
 * Locate the first occurrence of any character in @delim within
 * the string referenced by @input, and replace it with a NUL.
 * The location of the next character after the delimiter character
 * is stored into @input.
 * If the end of the string was reached without finding a delimiter
 * character, then NULL is stored into @input.
 * If @input points to a NULL pointer on entry, return NULL.
 * The return value is always the original value of *@input (and
 * so now points to a NUL-terminated string corresponding to the
 * part of the input up to the first delimiter).
 *
 * This function has the same behaviour as the BSD strsep() function.
 *
 * Returns: the pointer originally in @input.
 */
char *qemu_strsep(char **input, const char *delim);
time_t mktimegm(struct tm *tm);
int qemu_fdatasync(int fd);
int fcntl_setfl(int fd, int flag);
int qemu_parse_fd(const char *param);
int qemu_strtol(const char *nptr, const char **endptr, int base,
                long *result);
int qemu_strtoul(const char *nptr, const char **endptr, int base,
                 unsigned long *result);
int qemu_strtoll(const char *nptr, const char **endptr, int base,
                 int64_t *result);
int qemu_strtoull(const char *nptr, const char **endptr, int base,
                  uint64_t *result);

int parse_uint(const char *s, unsigned long long *value, char **endptr,
               int base);
int parse_uint_full(const char *s, unsigned long long *value, int base);

/*
 * qemu_strtosz() suffixes used to specify the default treatment of an
 * argument passed to qemu_strtosz() without an explicit suffix.
 * These should be defined using upper case characters in the range
 * A-Z, as qemu_strtosz() will use qemu_toupper() on the given argument
 * prior to comparison.
 */
#define QEMU_STRTOSZ_DEFSUFFIX_EB 'E'
#define QEMU_STRTOSZ_DEFSUFFIX_PB 'P'
#define QEMU_STRTOSZ_DEFSUFFIX_TB 'T'
#define QEMU_STRTOSZ_DEFSUFFIX_GB 'G'
#define QEMU_STRTOSZ_DEFSUFFIX_MB 'M'
#define QEMU_STRTOSZ_DEFSUFFIX_KB 'K'
#define QEMU_STRTOSZ_DEFSUFFIX_B 'B'
int64_t qemu_strtosz(const char *nptr, char **end);
int64_t qemu_strtosz_suffix(const char *nptr, char **end,
                            const char default_suffix);
int64_t qemu_strtosz_suffix_unit(const char *nptr, char **end,
                            const char default_suffix, int64_t unit);
#define K_BYTE     (1ULL << 10)
#define M_BYTE     (1ULL << 20)
#define G_BYTE     (1ULL << 30)
#define T_BYTE     (1ULL << 40)
#define P_BYTE     (1ULL << 50)
#define E_BYTE     (1ULL << 60)

/* used to print char* safely */
#define STR_OR_NULL(str) ((str) ? (str) : "null")

/* id.c */

typedef enum IdSubSystems {
    ID_QDEV,
    ID_BLOCK,
    ID_MAX      /* last element, used as array size */
} IdSubSystems;

char *id_generate(IdSubSystems id);
bool id_wellformed(const char *id);

/* path.c */
void init_paths(const char *prefix);
const char *path(const char *pathname);

#define qemu_isalnum(c)		isalnum((unsigned char)(c))
#define qemu_isalpha(c)		isalpha((unsigned char)(c))
#define qemu_iscntrl(c)		iscntrl((unsigned char)(c))
#define qemu_isdigit(c)		isdigit((unsigned char)(c))
#define qemu_isgraph(c)		isgraph((unsigned char)(c))
#define qemu_islower(c)		islower((unsigned char)(c))
#define qemu_isprint(c)		isprint((unsigned char)(c))
#define qemu_ispunct(c)		ispunct((unsigned char)(c))
#define qemu_isspace(c)		isspace((unsigned char)(c))
#define qemu_isupper(c)		isupper((unsigned char)(c))
#define qemu_isxdigit(c)	isxdigit((unsigned char)(c))
#define qemu_tolower(c)		tolower((unsigned char)(c))
#define qemu_toupper(c)		toupper((unsigned char)(c))
#define qemu_isascii(c)		isascii((unsigned char)(c))
#define qemu_toascii(c)		toascii((unsigned char)(c))

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

/* Error handling.  */

void QEMU_NORETURN hw_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

struct ParallelIOArg {
    void *buffer;
    int count;
};

typedef int (*DMA_transfer_handler) (void *opaque, int nchan, int pos, int size);

typedef uint64_t pcibus_t;

typedef struct PCIHostDeviceAddress {
    unsigned int domain;
    unsigned int bus;
    unsigned int slot;
    unsigned int function;
} PCIHostDeviceAddress;

void tcg_exec_init(unsigned long tb_size);
bool tcg_enabled(void);

void cpu_exec_init_all(void);

/* Unblock cpu */
void qemu_cpu_kick_self(void);

/* work queue */
struct qemu_work_item {
    struct qemu_work_item *next;
    void (*func)(void *data);
    void *data;
    int done;
    bool free;
};


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

typedef struct QEMUIOVector {
    struct iovec *iov;
    int niov;
    int nalloc;
    size_t size;
} QEMUIOVector;

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint);
void qemu_iovec_init_external(QEMUIOVector *qiov, struct iovec *iov, int niov);
void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len);
void qemu_iovec_concat(QEMUIOVector *dst,
                       QEMUIOVector *src, size_t soffset, size_t sbytes);
size_t qemu_iovec_concat_iov(QEMUIOVector *dst,
                             struct iovec *src_iov, unsigned int src_cnt,
                             size_t soffset, size_t sbytes);
bool qemu_iovec_is_zero(QEMUIOVector *qiov);
void qemu_iovec_destroy(QEMUIOVector *qiov);
void qemu_iovec_reset(QEMUIOVector *qiov);
size_t qemu_iovec_to_buf(QEMUIOVector *qiov, size_t offset,
                         void *buf, size_t bytes);
size_t qemu_iovec_from_buf(QEMUIOVector *qiov, size_t offset,
                           const void *buf, size_t bytes);
size_t qemu_iovec_memset(QEMUIOVector *qiov, size_t offset,
                         int fillc, size_t bytes);
ssize_t qemu_iovec_compare(QEMUIOVector *a, QEMUIOVector *b);
void qemu_iovec_clone(QEMUIOVector *dest, const QEMUIOVector *src, void *buf);
void qemu_iovec_discard_back(QEMUIOVector *qiov, size_t bytes);

bool buffer_is_zero(const void *buf, size_t len);

void qemu_progress_init(int enabled, float min_skip);
void qemu_progress_end(void);
void qemu_progress_print(float delta, int max);
const char *qemu_get_vm_name(void);

#define QEMU_FILE_TYPE_BIOS   0
#define QEMU_FILE_TYPE_KEYMAP 1
char *qemu_find_file(int type, const char *name);

/* OS specific functions */
void os_setup_early_signal_handling(void);
char *os_find_datadir(void);
void os_parse_cmd_args(int index, const char *optarg);

/* Convert a byte between binary and BCD.  */
static inline uint8_t to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static inline uint8_t from_bcd(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0f);
}

/* Round number down to multiple */
#define QEMU_ALIGN_DOWN(n, m) ((n) / (m) * (m))

/* Round number up to multiple */
#define QEMU_ALIGN_UP(n, m) QEMU_ALIGN_DOWN((n) + (m) - 1, (m))

#include "qemu/module.h"

/*
 * Implementation of ULEB128 (http://en.wikipedia.org/wiki/LEB128)
 * Input is limited to 14-bit numbers
 */

int uleb128_encode_small(uint8_t *out, uint32_t n);
int uleb128_decode_small(const uint8_t *in, uint32_t *n);

/* unicode.c */
int mod_utf8_codepoint(const char *s, size_t n, char **end);

/*
 * Hexdump a buffer to a file. An optional string prefix is added to every line
 */

void qemu_hexdump(const char *buf, FILE *fp, const char *prefix, size_t size);

/* vector definitions */
#ifdef __ALTIVEC__
#include <altivec.h>
/* The altivec.h header says we're allowed to undef these for
 * C++ compatibility.  Here we don't care about C++, but we
 * undef them anyway to avoid namespace pollution.
 */
#undef vector
#undef pixel
#undef bool
#define VECTYPE        __vector unsigned char
#define SPLAT(p)       vec_splat(vec_ld(0, p), 0)
#define ALL_EQ(v1, v2) vec_all_eq(v1, v2)
#define VEC_OR(v1, v2) ((v1) | (v2))
/* altivec.h may redefine the bool macro as vector type.
 * Reset it to POSIX semantics. */
#define bool _Bool
#elif defined __SSE2__
#include <emmintrin.h>
#define VECTYPE        __m128i
#define SPLAT(p)       _mm_set1_epi8(*(p))
#define ALL_EQ(v1, v2) (_mm_movemask_epi8(_mm_cmpeq_epi8(v1, v2)) == 0xFFFF)
#define VEC_OR(v1, v2) (_mm_or_si128(v1, v2))
#else
#define VECTYPE        unsigned long
#define SPLAT(p)       (*(p) * (~0UL / 255))
#define ALL_EQ(v1, v2) ((v1) == (v2))
#define VEC_OR(v1, v2) ((v1) | (v2))
#endif

#define BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR 8
bool can_use_buffer_find_nonzero_offset(const void *buf, size_t len);
size_t buffer_find_nonzero_offset(const void *buf, size_t len);

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
