#ifndef QEMU_LOG_H
#define QEMU_LOG_H

/* A small part of this API is split into its own header */
#include "qemu/log-for-trace.h"

/* 
 * The new API:
 */

/* Returns true if qemu_log() will really write somewhere. */
bool qemu_log_enabled(void);

/* Returns true if qemu_log() will write somewhere other than stderr. */
bool qemu_log_separate(void);

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
#define LOG_PER_THREAD     (1 << 20)

/* Lock/unlock output. */

FILE *qemu_log_trylock(void) G_GNUC_WARN_UNUSED_RESULT;
void qemu_log_unlock(FILE *fd);

/* Logging functions: */

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

bool qemu_set_log(int log_flags, Error **errp);
bool qemu_set_log_filename(const char *filename, Error **errp);
bool qemu_set_log_filename_flags(const char *name, int flags, Error **errp);
void qemu_set_dfilter_ranges(const char *ranges, Error **errp);
bool qemu_log_in_addr_range(uint64_t addr);
int qemu_str_to_log_mask(const char *str);

/* Print a usage message listing all the valid logging categories
 * to the specified FILE*.
 */
void qemu_print_log_usage(FILE *f);

#endif
