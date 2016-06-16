/*
 * memfd.c
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * QEMU library functions on POSIX which are shared between QEMU and
 * the QEMU tools.
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

#include "qemu/osdep.h"

#include <glib/gprintf.h>

#include "qemu/memfd.h"

#ifdef CONFIG_MEMFD
#include <sys/memfd.h>
#elif defined CONFIG_LINUX
#include <sys/syscall.h>
#include <asm/unistd.h>

static int memfd_create(const char *name, unsigned int flags)
{
#ifdef __NR_memfd_create
    return syscall(__NR_memfd_create, name, flags);
#else
    return -1;
#endif
}
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

/*
 * This is a best-effort helper for shared memory allocation, with
 * optional sealing. The helper will do his best to allocate using
 * memfd with sealing, but may fallback on other methods without
 * sealing.
 */
void *qemu_memfd_alloc(const char *name, size_t size, unsigned int seals,
                       int *fd)
{
    void *ptr;
    int mfd = -1;

    *fd = -1;

#ifdef CONFIG_LINUX
    if (seals) {
        mfd = memfd_create(name, MFD_ALLOW_SEALING | MFD_CLOEXEC);
    }

    if (mfd == -1) {
        /* some systems have memfd without sealing */
        mfd = memfd_create(name, MFD_CLOEXEC);
        seals = 0;
    }
#endif

    if (mfd != -1) {
        if (ftruncate(mfd, size) == -1) {
            perror("ftruncate");
            close(mfd);
            return NULL;
        }

        if (seals && fcntl(mfd, F_ADD_SEALS, seals) == -1) {
            perror("fcntl");
            close(mfd);
            return NULL;
        }
    } else {
        const char *tmpdir = g_get_tmp_dir();
        gchar *fname;

        fname = g_strdup_printf("%s/memfd-XXXXXX", tmpdir);
        mfd = mkstemp(fname);
        unlink(fname);
        g_free(fname);

        if (mfd == -1) {
            perror("mkstemp");
            return NULL;
        }

        if (ftruncate(mfd, size) == -1) {
            perror("ftruncate");
            close(mfd);
            return NULL;
        }
    }

    ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        close(mfd);
        return NULL;
    }

    *fd = mfd;
    return ptr;
}

void qemu_memfd_free(void *ptr, size_t size, int fd)
{
    if (ptr) {
        munmap(ptr, size);
    }

    if (fd != -1) {
        close(fd);
    }
}

enum {
    MEMFD_KO,
    MEMFD_OK,
    MEMFD_TODO
};

bool qemu_memfd_check(void)
{
    static int memfd_check = MEMFD_TODO;

    if (memfd_check == MEMFD_TODO) {
        int fd;
        void *ptr;

        ptr = qemu_memfd_alloc("test", 4096, 0, &fd);
        memfd_check = ptr ? MEMFD_OK : MEMFD_KO;
        qemu_memfd_free(ptr, 4096, fd);
    }

    return memfd_check == MEMFD_OK;
}
