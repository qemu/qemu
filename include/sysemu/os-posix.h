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

#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>

#ifdef CONFIG_SYSMACROS
#include <sys/sysmacros.h>
#endif

void os_set_line_buffering(void);
void os_set_proc_name(const char *s);
void os_setup_signal_handling(void);
void os_daemonize(void);
void os_setup_post(void);
int os_mlock(void);

#define closesocket(s) close(s)
#define ioctlsocket(s, r, v) ioctl(s, r, v)

typedef struct timeval qemu_timeval;
#define qemu_gettimeofday(tp) gettimeofday(tp, NULL)

bool is_daemonized(void);

/**
 * qemu_alloc_stack:
 * @sz: pointer to a size_t holding the requested usable stack size
 *
 * Allocate memory that can be used as a stack, for instance for
 * coroutines. If the memory cannot be allocated, this function
 * will abort (like g_malloc()). This function also inserts an
 * additional guard page to catch a potential stack overflow.
 * Note that the memory required for the guard page and alignment
 * and minimal stack size restrictions will increase the value of sz.
 *
 * The allocated stack must be freed with qemu_free_stack().
 *
 * Returns: pointer to (the lowest address of) the stack memory.
 */
void *qemu_alloc_stack(size_t *sz);

/**
 * qemu_free_stack:
 * @stack: stack to free
 * @sz: size of stack in bytes
 *
 * Free a stack allocated via qemu_alloc_stack(). Note that sz must
 * be exactly the adjusted stack size returned by qemu_alloc_stack.
 */
void qemu_free_stack(void *stack, size_t sz);

/* POSIX and Mingw32 differ in the name of the stdio lock functions.  */

static inline void qemu_flockfile(FILE *f)
{
    flockfile(f);
}

static inline void qemu_funlockfile(FILE *f)
{
    funlockfile(f);
}

#endif
