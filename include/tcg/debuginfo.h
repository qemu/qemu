/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_DEBUGINFO_H
#define TCG_DEBUGINFO_H

#include "qemu/bitops.h"

/*
 * Debuginfo describing a certain address.
 */
struct debuginfo_query {
    uint64_t address;    /* Input: address. */
    int flags;           /* Input: debuginfo subset. */
    const char *symbol;  /* Symbol that the address is part of. */
    uint64_t offset;     /* Offset from the symbol. */
    const char *file;    /* Source file associated with the address. */
    int line;            /* Line number in the source file. */
};

/*
 * Debuginfo subsets.
 */
#define DEBUGINFO_SYMBOL BIT(1)
#define DEBUGINFO_LINE   BIT(2)

#if defined(CONFIG_TCG) && defined(CONFIG_LIBDW)
/*
 * Load debuginfo for the specified guest ELF image.
 * Return true on success, false on failure.
 */
void debuginfo_report_elf(const char *name, int fd, uint64_t bias);

/*
 * Take the debuginfo lock.
 */
void debuginfo_lock(void);

/*
 * Fill each on N Qs with the debuginfo about Q->ADDRESS as specified by
 * Q->FLAGS:
 *
 * - DEBUGINFO_SYMBOL: update Q->SYMBOL and Q->OFFSET. If symbol debuginfo is
 *                     missing, then leave them as is.
 * - DEBUINFO_LINE: update Q->FILE and Q->LINE. If line debuginfo is missing,
 *                  then leave them as is.
 *
 * This function must be called under the debuginfo lock. The results can be
 * accessed only until the debuginfo lock is released.
 */
void debuginfo_query(struct debuginfo_query *q, size_t n);

/*
 * Release the debuginfo lock.
 */
void debuginfo_unlock(void);
#else
static inline void debuginfo_report_elf(const char *image_name, int image_fd,
                                        uint64_t load_bias)
{
}

static inline void debuginfo_lock(void)
{
}

static inline void debuginfo_query(struct debuginfo_query *q, size_t n)
{
}

static inline void debuginfo_unlock(void)
{
}
#endif

#endif
