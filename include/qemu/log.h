#ifndef QEMU_LOG_H
#define QEMU_LOG_H

/* A small part of this API is split into its own header */
#include "qemu/log-for-trace.h"
#include "qemu/rcu.h"

typedef struct QemuLogFile {
    struct rcu_head rcu;
    FILE *fd;
} QemuLogFile;

/* Private global variable, don't use */
extern QemuLogFile *qemu_logfile;


/* 
 * The new API:
 *
 */

/* Log settings checking macros: */

/* Returns true if qemu_log() will really write somewhere
 */
static inline bool qemu_log_enabled(void)
{
    return qemu_logfile != NULL;
}

/* Returns true if qemu_log() will write somewhere else than stderr
 */
static inline bool qemu_log_separate(void)
{
    QemuLogFile *logfile;
    bool res = false;

    rcu_read_lock();
    logfile = atomic_rcu_read(&qemu_logfile);
    if (logfile && logfile->fd != stderr) {
        res = true;
    }
    rcu_read_unlock();
    return res;
}

#define CPU_LOG_TB_OUT_ASM (1 << 0)
#define CPU_LOG_TB_IN_ASM  (1 << 1)
#define CPU_LOG_TB_OP      (1 << 2)
#define CPU_LOG_TB_OP_OPT  (1 << 3)
#define CPU_LOG_INT        (1 << 4)
#define CPU_LOG_EXEC       (1 << 5)
#define CPU_LOG_PCALL      (1 << 6)
#define CPU_LOG_TB_CPU     (1 << 8)
#define CPU_LOG_RESET      (1 << 9)
#define LOG_UNIMP          (1 << 10)
#define LOG_GUEST_ERROR    (1 << 11)
#define CPU_LOG_MMU        (1 << 12)
#define CPU_LOG_TB_NOCHAIN (1 << 13)
#define CPU_LOG_PAGE       (1 << 14)
/* LOG_TRACE (1 << 15) is defined in log-for-trace.h */
#define CPU_LOG_TB_OP_IND  (1 << 16)
#define CPU_LOG_TB_FPU     (1 << 17)
#define CPU_LOG_PLUGIN     (1 << 18)
/* LOG_STRACE is used for user-mode strace logging. */
#define LOG_STRACE         (1 << 19)

/* Lock output for a series of related logs.  Since this is not needed
 * for a single qemu_log / qemu_log_mask / qemu_log_mask_and_addr, we
 * assume that qemu_loglevel_mask has already been tested, and that
 * qemu_loglevel is never set when qemu_logfile is unset.
 */

static inline FILE *qemu_log_lock(void)
{
    QemuLogFile *logfile;
    rcu_read_lock();
    logfile = atomic_rcu_read(&qemu_logfile);
    if (logfile) {
        qemu_flockfile(logfile->fd);
        return logfile->fd;
    } else {
        return NULL;
    }
}

static inline void qemu_log_unlock(FILE *fd)
{
    if (fd) {
        qemu_funlockfile(fd);
    }
    rcu_read_unlock();
}

/* Logging functions: */

/* vfprintf-like logging function
 */
static inline void GCC_FMT_ATTR(1, 0)
qemu_log_vprintf(const char *fmt, va_list va)
{
    QemuLogFile *logfile;

    rcu_read_lock();
    logfile = atomic_rcu_read(&qemu_logfile);
    if (logfile) {
        vfprintf(logfile->fd, fmt, va);
    }
    rcu_read_unlock();
}

/* log only if a bit is set on the current loglevel mask:
 * @mask: bit to check in the mask
 * @fmt: printf-style format string
 * @args: optional arguments for format string
 */
#define qemu_log_mask(MASK, FMT, ...)                   \
    do {                                                \
        if (unlikely(qemu_loglevel_mask(MASK))) {       \
            qemu_log(FMT, ## __VA_ARGS__);              \
        }                                               \
    } while (0)

/* log only if a bit is set on the current loglevel mask
 * and we are in the address range we care about:
 * @mask: bit to check in the mask
 * @addr: address to check in dfilter
 * @fmt: printf-style format string
 * @args: optional arguments for format string
 */
#define qemu_log_mask_and_addr(MASK, ADDR, FMT, ...)    \
    do {                                                \
        if (unlikely(qemu_loglevel_mask(MASK)) &&       \
                     qemu_log_in_addr_range(ADDR)) {    \
            qemu_log(FMT, ## __VA_ARGS__);              \
        }                                               \
    } while (0)

/* Maintenance: */

/* define log items */
typedef struct QEMULogItem {
    int mask;
    const char *name;
    const char *help;
} QEMULogItem;

extern const QEMULogItem qemu_log_items[];

void qemu_set_log(int log_flags);
void qemu_log_needs_buffers(void);
void qemu_set_log_filename(const char *filename, Error **errp);
void qemu_set_dfilter_ranges(const char *ranges, Error **errp);
bool qemu_log_in_addr_range(uint64_t addr);
int qemu_str_to_log_mask(const char *str);

/* Print a usage message listing all the valid logging categories
 * to the specified FILE*.
 */
void qemu_print_log_usage(FILE *f);

/* fflush() the log file */
void qemu_log_flush(void);
/* Close the log file */
void qemu_log_close(void);

#endif
