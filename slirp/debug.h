/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#define DBG_CALL 0x1
#define DBG_MISC 0x2
#define DBG_ERROR 0x4

#define dfd stderr

extern int slirp_debug;

#define DEBUG_CALL(fmt, ...) do {               \
    if (slirp_debug & DBG_CALL) {               \
        fprintf(dfd, fmt, ##__VA_ARGS__);       \
        fprintf(dfd, "...\n");                  \
        fflush(dfd);                            \
    }                                           \
} while (0)

#define DEBUG_ARG(fmt, ...) do {                \
    if (slirp_debug & DBG_CALL) {               \
        fputc(' ', dfd);                        \
        fprintf(dfd, fmt, ##__VA_ARGS__);       \
        fputc('\n', dfd);                       \
        fflush(dfd);                            \
    }                                           \
} while (0)

#define DEBUG_MISC(fmt, ...) do {               \
    if (slirp_debug & DBG_MISC) {               \
        fprintf(dfd, fmt, ##__VA_ARGS__);       \
        fflush(dfd);                            \
    }                                           \
} while (0)

#define DEBUG_ERROR(fmt, ...) do {              \
    if (slirp_debug & DBG_ERROR) {              \
        fprintf(dfd, fmt, ##__VA_ARGS__);       \
        fflush(dfd);                            \
    }                                           \
} while (0)

#endif /* DEBUG_H_ */
