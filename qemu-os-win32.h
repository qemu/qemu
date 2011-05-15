/*
 * win32 specific declarations
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_OS_WIN32_H
#define QEMU_OS_WIN32_H

#include <windows.h>
#include <winsock2.h>

/* Declaration of ffs() is missing in MinGW's strings.h. */
int ffs(int i);

/* Polling handling */

/* return TRUE if no sleep should be done afterwards */
typedef int PollingFunc(void *opaque);

int qemu_add_polling_cb(PollingFunc *func, void *opaque);
void qemu_del_polling_cb(PollingFunc *func, void *opaque);

/* Wait objects handling */
typedef void WaitObjectFunc(void *opaque);

int qemu_add_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque);
void qemu_del_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque);

void os_host_main_loop_wait(int *timeout);

static inline void os_setup_signal_handling(void) {}
static inline void os_daemonize(void) {}
static inline void os_setup_post(void) {}
void os_set_line_buffering(void);
static inline void os_set_proc_name(const char *dummy) {}

#if !defined(EPROTONOSUPPORT)
# define EPROTONOSUPPORT EINVAL
#endif

int setenv(const char *name, const char *value, int overwrite);

typedef struct {
    long tv_sec;
    long tv_usec;
} qemu_timeval;
int qemu_gettimeofday(qemu_timeval *tp);

#endif
