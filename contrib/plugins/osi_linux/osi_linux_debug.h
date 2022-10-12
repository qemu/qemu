/*!
 * @file osi_linux_debug.h
 * @brief Macros for debugging the implementation of Linux OSI.
 *
 * @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>
 * @copyright This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */
#pragma once
#include <stdint.h>

/**
 *  @brief Debug macros.
 */
#define HEXDUMP(_buf, _size, _base) \
    { \
        uintptr_t _b = (uintptr_t)_base; \
        _b = (_b == 0) ? (uintptr_t)_buf : _b; \
        for (uint32_t _i=0; _i<_size;) { \
            if (_i % 16 == 0) { printf("%" PRIxPTR "\t", (uintptr_t)_b + _i); } \
            printf("%02x", ((uint8_t *)_buf)[_i]); \
            _i++; \
            if (_i % 16 == 0) { printf("\n"); continue; } \
            else if (_i % 8 == 0) { printf("  "); continue; } \
            else { printf(" "); } \
        } \
        if (_size % 16 != 0) { printf("\n"); } \
    }

/**
 * @brief Macros to check if a task_struct is a thread (T) or process (P).
 */
#define TS_THREAD(env, ts) ((ts + ki.task.thread_group_offset != get_thread_group(env, ts)) ? 1 : 0)
#define TS_THREAD_CHR(env, ts) (TS_THREAD(env, ts_current) ? 'T' : 'P')

/**
 * @brief Macros to check if a task_struct is a thread group leader (L) or follower (F).
 */
#define TS_LEADER(env, ts) ((get_pid(env, ts) == get_tgid(env, ts)) ? 1 : 0)
#define TS_LEADER_CHR(env, ts) (TS_LEADER(env, ts_current) ? 'L' : 'F')

/** @brief Marker for dynamic names. */
#define DNAME_MARK "§"

/**
 * @brief Maximum number of processes. Rough way to detect infinite loops
 * when iterating the process list. Undefining the macro will disable the
 * checks.
 */
#ifndef OSI_MAX_PROC
#define OSI_MAX_PROC 0
#endif

/**
 * @brief Macro that checks if \p n has exceeded OSI_MAX_PROC and break
 * out of the current loop after printing the message \p s.
 */
#if OSI_MAX_PROC > 0
#define OSI_MAX_PROC_CHECK(n, s) {\
    uint32_t __n = (n);\
    if (__n > OSI_MAX_PROC) {\
        fprintf(stderr, PANDA_MSG "Potential infinite loop at instruction %" PRId64 " while " s ". Breaking out.\n", rr_get_guest_instr_count());\
        break;\
    }\
}
#else
#define OSI_MAX_PROC_CHECK(n, s)
#endif

/**
 * @brief Returns the number of pages required to store n bytes.
 */
#define NPAGES(n) ((uint32_t)((n) >> 12))

/* vim:set tabstop=4 softtabstop=4 expandtab: */
