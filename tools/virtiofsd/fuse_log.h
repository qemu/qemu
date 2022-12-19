/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2019  Red Hat, Inc.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB.
 */

#ifndef FUSE_LOG_H_
#define FUSE_LOG_H_

/** @file
 *
 * This file defines the logging interface of FUSE
 */


/**
 * Log severity level
 *
 * These levels correspond to syslog(2) log levels since they are widely used.
 */
enum fuse_log_level {
    FUSE_LOG_EMERG,
    FUSE_LOG_ALERT,
    FUSE_LOG_CRIT,
    FUSE_LOG_ERR,
    FUSE_LOG_WARNING,
    FUSE_LOG_NOTICE,
    FUSE_LOG_INFO,
    FUSE_LOG_DEBUG
};

/**
 * Log message handler function.
 *
 * This function must be thread-safe.  It may be called from any libfuse
 * function, including fuse_parse_cmdline() and other functions invoked before
 * a FUSE filesystem is created.
 *
 * Install a custom log message handler function using fuse_set_log_func().
 *
 * @param level log severity level
 * @param fmt sprintf-style format string including newline
 * @param ap format string arguments
 */
typedef void (*fuse_log_func_t)(enum fuse_log_level level, const char *fmt,
                                va_list ap)
    G_GNUC_PRINTF(2, 0);

/**
 * Install a custom log handler function.
 *
 * Log messages are emitted by libfuse functions to report errors and debug
 * information.  Messages are printed to stderr by default but this can be
 * overridden by installing a custom log message handler function.
 *
 * The log message handler function is global and affects all FUSE filesystems
 * created within this process.
 *
 * @param func a custom log message handler function or NULL to revert to
 *             the default
 */
void fuse_set_log_func(fuse_log_func_t func);

/**
 * Emit a log message
 *
 * @param level severity level (FUSE_LOG_ERR, FUSE_LOG_DEBUG, etc)
 * @param fmt sprintf-style format string including newline
 */
void fuse_log(enum fuse_log_level level, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

#endif /* FUSE_LOG_H_ */
