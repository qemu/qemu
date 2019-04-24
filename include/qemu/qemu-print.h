/*
 * Print to stream or current monitor
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PRINT_H
#define QEMU_PRINT_H

int qemu_vprintf(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);
int qemu_printf(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

int qemu_vfprintf(FILE *stream, const char *fmt, va_list ap)
    GCC_FMT_ATTR(2, 0);
int qemu_fprintf(FILE *stream, const char *fmt, ...) GCC_FMT_ATTR(2, 3);

#endif
