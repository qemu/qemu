/*
 *  mmap support for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "qemu.h"

//#define DEBUG_MMAP

/* NOTE: all the constants are the HOST ones */
int target_mprotect(unsigned long start, unsigned long len, int prot)
{
    unsigned long end, host_start, host_end, addr;
    int prot1, ret;

#ifdef DEBUG_MMAP
    printf("mprotect: start=0x%lx len=0x%lx prot=%c%c%c\n", start, len,
           prot & PROT_READ ? 'r' : '-',
           prot & PROT_WRITE ? 'w' : '-',
           prot & PROT_EXEC ? 'x' : '-');
#endif

    if ((start & ~TARGET_PAGE_MASK) != 0)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    end = start + len;
    if (end < start)
        return -EINVAL;
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
        return -EINVAL;
    if (len == 0)
        return 0;

    host_start = start & qemu_host_page_mask;
    host_end = HOST_PAGE_ALIGN(end);
    if (start > host_start) {
        /* handle host page containing start */
        prot1 = prot;
        for(addr = host_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        if (host_end == host_start + qemu_host_page_size) {
            for(addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
                prot1 |= page_get_flags(addr);
            }
            end = host_end;
        }
        ret = mprotect((void *)host_start, qemu_host_page_size, prot1 & PAGE_BITS);
        if (ret != 0)
            return ret;
        host_start += qemu_host_page_size;
    }
    if (end < host_end) {
        prot1 = prot;
        for(addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        ret = mprotect((void *)(host_end - qemu_host_page_size), qemu_host_page_size,
                       prot1 & PAGE_BITS);
        if (ret != 0)
            return ret;
        host_end -= qemu_host_page_size;
    }

    /* handle the pages in the middle */
    if (host_start < host_end) {
        ret = mprotect((void *)host_start, host_end - host_start, prot);
        if (ret != 0)
            return ret;
    }
    page_set_flags(start, start + len, prot | PAGE_VALID);
    return 0;
}

/* map an incomplete host page */
int mmap_frag(unsigned long host_start,
               unsigned long start, unsigned long end,
               int prot, int flags, int fd, unsigned long offset)
{
    unsigned long host_end, ret, addr;
    int prot1, prot_new;

    host_end = host_start + qemu_host_page_size;

    /* get the protection of the target pages outside the mapping */
    prot1 = 0;
    for(addr = host_start; addr < host_end; addr++) {
        if (addr < start || addr >= end)
            prot1 |= page_get_flags(addr);
    }

    if (prot1 == 0) {
        /* no page was there, so we allocate one */
        ret = (long)mmap((void *)host_start, qemu_host_page_size, prot,
                         flags | MAP_ANONYMOUS, -1, 0);
        if (ret == -1)
            return ret;
    }
    prot1 &= PAGE_BITS;

    prot_new = prot | prot1;
    if (!(flags & MAP_ANONYMOUS)) {
        /* msync() won't work here, so we return an error if write is
           possible while it is a shared mapping */
#ifndef __APPLE__
        if ((flags & MAP_TYPE) == MAP_SHARED &&
#else
        if ((flags &  MAP_SHARED) &&
#endif
            (prot & PROT_WRITE))
            return -EINVAL;

        /* adjust protection to be able to read */
        if (!(prot1 & PROT_WRITE))
            mprotect((void *)host_start, qemu_host_page_size, prot1 | PROT_WRITE);

        /* read the corresponding file data */
        pread(fd, (void *)start, end - start, offset);

        /* put final protection */
        if (prot_new != (prot1 | PROT_WRITE))
            mprotect((void *)host_start, qemu_host_page_size, prot_new);
    } else {
        /* just update the protection */
        if (prot_new != prot1) {
            mprotect((void *)host_start, qemu_host_page_size, prot_new);
        }
    }
    return 0;
}

/* NOTE: all the constants are the HOST ones */
long target_mmap(unsigned long start, unsigned long len, int prot,
                 int flags, int fd, unsigned long offset)
{
    unsigned long ret, end, host_start, host_end, retaddr, host_offset, host_len;
#if defined(__alpha__) || defined(__sparc__) || defined(__x86_64__)
    static unsigned long last_start = 0x40000000;
#endif

#ifdef DEBUG_MMAP
    {
        printf("mmap: start=0x%lx len=0x%lx prot=%c%c%c flags=",
               start, len,
               prot & PROT_READ ? 'r' : '-',
               prot & PROT_WRITE ? 'w' : '-',
               prot & PROT_EXEC ? 'x' : '-');
        if (flags & MAP_FIXED)
            printf("MAP_FIXED ");
        if (flags & MAP_ANONYMOUS)
            printf("MAP_ANON ");
#ifndef MAP_TYPE
# define MAP_TYPE 0x3
#endif
        switch(flags & MAP_TYPE) {
        case MAP_PRIVATE:
            printf("MAP_PRIVATE ");
            break;
        case MAP_SHARED:
            printf("MAP_SHARED ");
            break;
        default:
            printf("[MAP_TYPE=0x%x] ", flags & MAP_TYPE);
            break;
        }
        printf("fd=%d offset=%lx\n", fd, offset);
    }
#endif

    if (offset & ~TARGET_PAGE_MASK)
        return -EINVAL;

    len = TARGET_PAGE_ALIGN(len);
    if (len == 0)
        return start;
    host_start = start & qemu_host_page_mask;

    if (!(flags & MAP_FIXED)) {
#if defined(__alpha__) || defined(__sparc__) || defined(__x86_64__)
        /* tell the kernel to search at the same place as i386 */
        if (host_start == 0) {
            host_start = last_start;
            last_start += HOST_PAGE_ALIGN(len);
        }
#endif
        if (qemu_host_page_size != qemu_real_host_page_size) {
            /* NOTE: this code is only for debugging with '-p' option */
            /* reserve a memory area */
            host_len = HOST_PAGE_ALIGN(len) + qemu_host_page_size - TARGET_PAGE_SIZE;
            host_start = (long)mmap((void *)host_start, host_len, PROT_NONE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (host_start == -1)
                return host_start;
            host_end = host_start + host_len;
            start = HOST_PAGE_ALIGN(host_start);
            end = start + HOST_PAGE_ALIGN(len);
            if (start > host_start)
                munmap((void *)host_start, start - host_start);
            if (end < host_end)
                munmap((void *)end, host_end - end);
            /* use it as a fixed mapping */
            flags |= MAP_FIXED;
        } else {
            /* if not fixed, no need to do anything */
            host_offset = offset & qemu_host_page_mask;
            host_len = len + offset - host_offset;
            start = (long)mmap((void *)host_start, host_len,
                               prot, flags, fd, host_offset);
            if (start == -1)
                return start;
            /* update start so that it points to the file position at 'offset' */
            if (!(flags & MAP_ANONYMOUS))
                start += offset - host_offset;
            goto the_end1;
        }
    }

    if (start & ~TARGET_PAGE_MASK)
        return -EINVAL;
    end = start + len;
    host_end = HOST_PAGE_ALIGN(end);

    /* worst case: we cannot map the file because the offset is not
       aligned, so we read it */
    if (!(flags & MAP_ANONYMOUS) &&
        (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
        /* msync() won't work here, so we return an error if write is
           possible while it is a shared mapping */
#ifndef __APPLE__
        if ((flags & MAP_TYPE) == MAP_SHARED &&
#else
        if ((flags & MAP_SHARED) &&
#endif
            (prot & PROT_WRITE))
            return -EINVAL;
        retaddr = target_mmap(start, len, prot | PROT_WRITE,
                              MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                              -1, 0);
        if (retaddr == -1)
            return retaddr;
        pread(fd, (void *)start, len, offset);
        if (!(prot & PROT_WRITE)) {
            ret = target_mprotect(start, len, prot);
            if (ret != 0)
                return ret;
        }
        goto the_end;
    }

    /* handle the start of the mapping */
    if (start > host_start) {
        if (host_end == host_start + qemu_host_page_size) {
            /* one single host page */
            ret = mmap_frag(host_start, start, end,
                            prot, flags, fd, offset);
            if (ret == -1)
                return ret;
            goto the_end1;
        }
        ret = mmap_frag(host_start, start, host_start + qemu_host_page_size,
                        prot, flags, fd, offset);
        if (ret == -1)
            return ret;
        host_start += qemu_host_page_size;
    }
    /* handle the end of the mapping */
    if (end < host_end) {
        ret = mmap_frag(host_end - qemu_host_page_size,
                        host_end - qemu_host_page_size, host_end,
                        prot, flags, fd,
                        offset + host_end - qemu_host_page_size - start);
        if (ret == -1)
            return ret;
        host_end -= qemu_host_page_size;
    }

    /* map the middle (easier) */
    if (host_start < host_end) {
        unsigned long offset1;
	if (flags & MAP_ANONYMOUS)
	  offset1 = 0;
	else
	  offset1 = offset + host_start - start;
        ret = (long)mmap((void *)host_start, host_end - host_start,
                         prot, flags, fd, offset1);
        if (ret == -1)
            return ret;
    }
 the_end1:
    page_set_flags(start, start + len, prot | PAGE_VALID);
 the_end:
#ifdef DEBUG_MMAP
    printf("target_mmap: ret=0x%lx\n", (long)start);
    page_dump(stdout);
    printf("\n");
#endif
    return start;
}

int target_munmap(unsigned long start, unsigned long len)
{
    unsigned long end, host_start, host_end, addr;
    int prot, ret;

#ifdef DEBUG_MMAP
    printf("munmap: start=0x%lx len=0x%lx\n", start, len);
#endif
    if (start & ~TARGET_PAGE_MASK)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    if (len == 0)
        return -EINVAL;
    end = start + len;
    host_start = start & qemu_host_page_mask;
    host_end = HOST_PAGE_ALIGN(end);

    if (start > host_start) {
        /* handle host page containing start */
        prot = 0;
        for(addr = host_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (host_end == host_start + qemu_host_page_size) {
            for(addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
                prot |= page_get_flags(addr);
            }
            end = host_end;
        }
        if (prot != 0)
            host_start += qemu_host_page_size;
    }
    if (end < host_end) {
        prot = 0;
        for(addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (prot != 0)
            host_end -= qemu_host_page_size;
    }

    /* unmap what we can */
    if (host_start < host_end) {
        ret = munmap((void *)host_start, host_end - host_start);
        if (ret != 0)
            return ret;
    }

    page_set_flags(start, start + len, 0);
    return 0;
}

/* XXX: currently, we only handle MAP_ANONYMOUS and not MAP_FIXED
   blocks which have been allocated starting on a host page */
long target_mremap(unsigned long old_addr, unsigned long old_size,
                   unsigned long new_size, unsigned long flags,
                   unsigned long new_addr)
{
#ifndef __APPLE__
    /* XXX: use 5 args syscall */
    new_addr = (long)mremap((void *)old_addr, old_size, new_size, flags);
    if (new_addr == -1)
        return new_addr;
    prot = page_get_flags(old_addr);
    page_set_flags(old_addr, old_addr + old_size, 0);
    page_set_flags(new_addr, new_addr + new_size, prot | PAGE_VALID);
    return new_addr;
#else
    qerror("target_mremap: unsupported\n");
#endif

}

int target_msync(unsigned long start, unsigned long len, int flags)
{
    unsigned long end;

    if (start & ~TARGET_PAGE_MASK)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    end = start + len;
    if (end < start)
        return -EINVAL;
    if (end == start)
        return 0;

    start &= qemu_host_page_mask;
    return msync((void *)start, end - start, flags);
}
