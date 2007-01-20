/*
 * QEMU low level functions
 * 
 * Copyright (c) 2003 Fabrice Bellard
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifdef HOST_SOLARIS
#include <sys/types.h>
#include <sys/statvfs.h>
#endif

#include "cpu.h"
#if defined(USE_KQEMU)
#include "vl.h"
#endif

#ifdef _WIN32
#include <windows.h>
#elif defined(_BSD)
#include <stdlib.h>
#else
#include <malloc.h>
#endif

void *get_mmap_addr(unsigned long size)
{
    return NULL;
}

void qemu_free(void *ptr)
{
    free(ptr);
}

void *qemu_malloc(size_t size)
{
    return malloc(size);
}

#if defined(_WIN32)

void *qemu_vmalloc(size_t size)
{
    /* FIXME: this is not exactly optimal solution since VirtualAlloc
       has 64Kb granularity, but at least it guarantees us that the
       memory is page aligned. */
    return VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
}

void qemu_vfree(void *ptr)
{
    VirtualFree(ptr, 0, MEM_RELEASE);
}

#else

#if defined(USE_KQEMU)

#include <sys/vfs.h>
#include <sys/mman.h>
#include <fcntl.h>

void *kqemu_vmalloc(size_t size)
{
    static int phys_ram_fd = -1;
    static int phys_ram_size = 0;
    const char *tmpdir;
    char phys_ram_file[1024];
    void *ptr;
#ifdef HOST_SOLARIS
    struct statvfs stfs;
#else
    struct statfs stfs;
#endif

    if (phys_ram_fd < 0) {
        tmpdir = getenv("QEMU_TMPDIR");
        if (!tmpdir)
#ifdef HOST_SOLARIS
            tmpdir = "/tmp";
        if (statvfs(tmpdir, &stfs) == 0) {
#else
            tmpdir = "/dev/shm";
        if (statfs(tmpdir, &stfs) == 0) {
#endif
            int64_t free_space;
            int ram_mb;

            extern int ram_size;
            free_space = (int64_t)stfs.f_bavail * stfs.f_bsize;
            if ((ram_size + 8192 * 1024) >= free_space) {
                ram_mb = (ram_size / (1024 * 1024));
                fprintf(stderr, 
                        "You do not have enough space in '%s' for the %d MB of QEMU virtual RAM.\n",
                        tmpdir, ram_mb);
                if (strcmp(tmpdir, "/dev/shm") == 0) {
                    fprintf(stderr, "To have more space available provided you have enough RAM and swap, do as root:\n"
                            "umount /dev/shm\n"
                            "mount -t tmpfs -o size=%dm none /dev/shm\n",
                            ram_mb + 16);
                } else {
                    fprintf(stderr, 
                            "Use the '-m' option of QEMU to diminish the amount of virtual RAM or use the\n"
                            "QEMU_TMPDIR environment variable to set another directory where the QEMU\n"
                            "temporary RAM file will be opened.\n");
                }
                fprintf(stderr, "Or disable the accelerator module with -no-kqemu\n");
                exit(1);
            }
        }
        snprintf(phys_ram_file, sizeof(phys_ram_file), "%s/qemuXXXXXX", 
                 tmpdir);
        phys_ram_fd = mkstemp(phys_ram_file);
        if (phys_ram_fd < 0) {
            fprintf(stderr, 
                    "warning: could not create temporary file in '%s'.\n"
                    "Use QEMU_TMPDIR to select a directory in a tmpfs filesystem.\n"
                    "Using '/tmp' as fallback.\n",
                    tmpdir);
            snprintf(phys_ram_file, sizeof(phys_ram_file), "%s/qemuXXXXXX", 
                     "/tmp");
            phys_ram_fd = mkstemp(phys_ram_file);
            if (phys_ram_fd < 0) {
                fprintf(stderr, "Could not create temporary memory file '%s'\n", 
                        phys_ram_file);
                exit(1);
            }
        }
        unlink(phys_ram_file);
    }
    size = (size + 4095) & ~4095;
    ftruncate(phys_ram_fd, phys_ram_size + size);
    ptr = mmap(NULL, 
               size, 
               PROT_WRITE | PROT_READ, MAP_SHARED, 
               phys_ram_fd, phys_ram_size);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "Could not map physical memory\n");
        exit(1);
    }
    phys_ram_size += size;
    return ptr;
}

void kqemu_vfree(void *ptr)
{
    /* may be useful some day, but currently we do not need to free */
}

#endif

/* alloc shared memory pages */
void *qemu_vmalloc(size_t size)
{
#if defined(USE_KQEMU)
    if (kqemu_allowed)
        return kqemu_vmalloc(size);
#endif
#ifdef _BSD
    return valloc(size);
#else
    return memalign(4096, size);
#endif
}

void qemu_vfree(void *ptr)
{
#if defined(USE_KQEMU)
    if (kqemu_allowed)
        kqemu_vfree(ptr);
#endif
    free(ptr);
}

#endif

void *qemu_mallocz(size_t size)
{
    void *ptr;
    ptr = qemu_malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

char *qemu_strdup(const char *str)
{
    char *ptr;
    ptr = qemu_malloc(strlen(str) + 1);
    if (!ptr)
        return NULL;
    strcpy(ptr, str);
    return ptr;
}
