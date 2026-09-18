/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_TESTS_S390X_STRING_H
#define QEMU_TESTS_S390X_STRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t size);

#endif
