/*
 * posix specific declarations
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

#ifndef QEMU_OS_POSIX_H
#define QEMU_OS_POSIX_H

void os_set_line_buffering(void);
void os_set_proc_name(const char *s);
void os_setup_signal_handling(void);
void os_daemonize(void);
void os_setup_post(void);

typedef struct timeval qemu_timeval;
#define qemu_gettimeofday(tp) gettimeofday(tp, NULL)

#ifndef CONFIG_UTIMENSAT
#ifndef UTIME_NOW
# define UTIME_NOW     ((1l << 30) - 1l)
#endif
#ifndef UTIME_OMIT
# define UTIME_OMIT    ((1l << 30) - 2l)
#endif
#endif
typedef struct timespec qemu_timespec;
int qemu_utimens(const char *path, const qemu_timespec *times);

#endif
