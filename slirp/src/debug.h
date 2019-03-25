/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 1995 Danny Gasparovski.
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#define DBG_CALL (1 << 0)
#define DBG_MISC (1 << 1)
#define DBG_ERROR (1 << 2)
#define DBG_TFTP (1 << 3)

extern int slirp_debug;

#define DEBUG_CALL(fmt, ...) do {               \
    if (G_UNLIKELY(slirp_debug & DBG_CALL)) {   \
        g_debug(fmt "...", ##__VA_ARGS__);      \
    }                                           \
} while (0)

#define DEBUG_ARG(fmt, ...) do {                \
    if (G_UNLIKELY(slirp_debug & DBG_CALL)) {   \
        g_debug(" " fmt, ##__VA_ARGS__);        \
    }                                           \
} while (0)

#define DEBUG_MISC(fmt, ...) do {               \
    if (G_UNLIKELY(slirp_debug & DBG_MISC)) {   \
        g_debug(fmt, ##__VA_ARGS__);            \
    }                                           \
} while (0)

#define DEBUG_ERROR(fmt, ...) do {              \
    if (G_UNLIKELY(slirp_debug & DBG_ERROR)) {  \
        g_debug(fmt, ##__VA_ARGS__);            \
    }                                           \
} while (0)

#define DEBUG_TFTP(fmt, ...) do {               \
    if (G_UNLIKELY(slirp_debug & DBG_TFTP)) {   \
        g_debug(fmt, ##__VA_ARGS__);            \
    }                                           \
} while (0)

#endif /* DEBUG_H_ */
