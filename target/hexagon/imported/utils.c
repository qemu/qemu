/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


/* 
 * utils.c
 * simple utility functions used by qsim
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "utils.h"

/* 
 * err_info 
 *
 * Calls to this function are generated from the "info" macro in utils.h
 *
 * Warn the user about some condition, but do not exit the program 
 */
void err_info(const char *func, const char *file, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "INFO: %s in %s:%d:", func, file, line);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	fflush(stderr);
}

/* 
 * err_warn 
 *
 * Calls to this function are generated from the "warn" macro in utils.h
 *
 * Warn the user about some condition, but do not exit the program 
 */
void err_warn(const char *func, const char *file, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "WARNING: %s in %s:%d:", func, file, line);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	fflush(stderr);
}

/* 
 * err_fatal 
 *
 * Calls to this function are generated from the "fatal" macro in utils.h
 *
 * Some unrecoverable error condition exists, exit the program.
 */
void err_fatal(const char *func, const char *file, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "FATAL: %s in %s:%d:", func, file, line);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	fflush(stderr);
	exit(1);
}


/* 
 * err_panic 
 *
 * Calls to this function are generated from the "panic" macro in utils.h
 *
 * Some bad condition exists, abort() the program (generate core dump)
 */
void err_panic(const char *func, const char *file, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "PANIC: %s in %s:%d:", func, file, line);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	fflush(stderr);
	abort();
}

#ifdef FIXME
void err_debug(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fflush(stderr);
}
#endif
