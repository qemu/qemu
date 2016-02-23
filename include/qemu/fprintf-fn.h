/*
 * Typedef for fprintf-alike function pointers.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_FPRINTF_FN_H
#define QEMU_FPRINTF_FN_H 1


typedef int (*fprintf_function)(FILE *f, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

#endif
