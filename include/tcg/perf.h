/*
 * Linux perf perf-<pid>.map and jit-<pid>.dump integration.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_PERF_H
#define TCG_PERF_H

#if defined(CONFIG_TCG) && defined(CONFIG_LINUX)
/* Start writing perf-<pid>.map. */
void perf_enable_perfmap(void);

/* Start writing jit-<pid>.dump. */
void perf_enable_jitdump(void);

/* Add information about TCG prologue to profiler maps. */
void perf_report_prologue(const void *start, size_t size);

/* Add information about JITted guest code to profiler maps. */
void perf_report_code(uint64_t guest_pc, TranslationBlock *tb,
                      const void *start);

/* Stop writing perf-<pid>.map and/or jit-<pid>.dump. */
void perf_exit(void);
#else
static inline void perf_enable_perfmap(void)
{
}

static inline void perf_enable_jitdump(void)
{
}

static inline void perf_report_prologue(const void *start, size_t size)
{
}

static inline void perf_report_code(uint64_t guest_pc, TranslationBlock *tb,
                                    const void *start)
{
}

static inline void perf_exit(void)
{
}
#endif

#endif
