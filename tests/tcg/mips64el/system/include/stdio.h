/*
 * Freestanding <stdio.h> for bare-metal R5900 tests
 *
 * There is no console: fprintf() only records its format string,
 * which the gdb script reports when the test fails.
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef R5900_SYSTEM_STDIO_H
#define R5900_SYSTEM_STDIO_H

void test_message(const char *msg);

#define fprintf(stream, fmt, ...) test_message(fmt)

#endif
