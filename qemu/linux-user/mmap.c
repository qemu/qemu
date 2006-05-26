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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/* NOTE: all the constants are the HOST ones, but addresses are target. */
int target_mprotect(target_ulong start, target_ulong len, int prot)
{
    target_ulong end, host_start, host_end, addr;
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
        ret = mprotect(g2h(host_start), qemu_host_page_size, prot1 & PAGE_BITS);
        if (ret != 0)
            return ret;
        host_start += qemu_host_page_size;
    }
    if (end < host_end) {
        prot1 = prot;
        for(addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        ret = mprotect(g2h(host_end - qemu_host_page_size), qemu_host_page_size, 
                       prot1 & PAGE_BITS);
        if (ret != 0)
            return ret;
        host_end -= qemu_host_page_size;
    }
    
    /* handle the pages in the middle */
    if (host_start < host_end) {
        ret = mprotect(g2h(host_start), host_end - host_start, prot);
        if (ret != 0)
            return ret;
    }
    page_set_flags(start, start + len, prot | PAGE_VALID);
    return 0;
}

/* map an incomplete host page */
static int mmap_frag(target_ulong real_start, 
                     target_ulong start, target_ulong end, 
                     int prot, int flags, int fd, target_ulong offset)
{
    target_ulong real_end, ret, addr;
    void *host_start;
    int prot1, prot_new;

    real_end = real_start + qemu_host_page_size;
    host_start = g2h(real_start);

    /* get the protection of the target pages outside the mapping */
    prot1 = 0;
    for(addr = real_start; addr < real_end; addr++) {
        if (addr < start || addr >= end)
            prot1 |= page_get_flags(addr);
    }
    
    if (prot1 == 0) {
        /* no page was there, so we allocate one */
        ret = (long)mmap(host_start, qemu_host_page_size, prot, 
                         flags | MAP_ANONYMOUS, -1, 0);
        if (ret == -1)
            return ret;
        prot1 = prot;
    }
    prot1 &= PAGE_BITS;

    prot_new = prot | prot1;
    if (!(flags & MAP_ANONYMOUS)) {
        /* msync() won't work here, so we return an error if write is
           possible while it is a shared mapping */
        if ((flags & MAP_TYPE) == MAP_SHARED &&
            (prot & PROT_WRITE))
            return -EINVAL;

        /* adjust protection to be able to read */
        if (!(prot1 & PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot1 | PROT_WRITE);
        
        /* read the corresponding file data */
        pread(fd, g2h(start), end - start, offset);
        
        /* put final protection */
        if (prot_new != (prot1 | PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot_new);
    } else {
        /* just update the protection */
        if (prot_new != prot1) {
            mprotect(host_start, qemu_host_page_size, prot_new);
        }
    }
    return 0;
}

/* NOTE: all the constants are the HOST ones */
long target_mmap(target_ulong start, target_ulong len, int prot, 
                 int flags, int fd, target_ulong offset)
{
    target_ulong ret, end, real_start, real_end, retaddr, host_offset, host_len;
    long host_start;
#if defined(__alpha__) || defined(__sparc__) || defined(__x86_64__) || \
    defined(__ia64)
    static target_ulong last_start = 0x40000000;
#elif defined(__CYGWIN__)
    /* Cygwin doesn't have a whole lot of address space.  */
    static target_ulong last_start = 0x18000000;
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

    if (offset & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        return -1;
    }

    len = TARGET_PAGE_ALIGN(len);
    if (len == 0)
        return start;
    real_start = start & qemu_host_page_mask;

    if (!(flags & MAP_FIXED)) {
#if defined(__alpha__) || defined(__sparc__) || defined(__x86_64__) || \
    defined(__ia64) || defined(__CYGWIN__)
        /* tell the kenel to search at the same place as i386 */
        if (real_start == 0) {
            real_start = last_start;
            last_start += HOST_PAGE_ALIGN(len);
        }
#endif
        if (qemu_host_page_size != qemu_real_host_page_size) {
            /* NOTE: this code is only for debugging with '-p' option */
            /* ??? Can also occur when TARGET_PAGE_SIZE > host page size.  */
            /* reserve a memory area */
            /* ??? This needs fixing for remapping.  */
abort();
            host_len = HOST_PAGE_ALIGN(len) + qemu_host_page_size - TARGET_PAGE_SIZE;
            real_start = (long)mmap(g2h(real_start), host_len, PROT_NONE, 
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (real_start == -1)
                return real_start;
            real_end = real_start + host_len;
            start = HOST_PAGE_ALIGN(real_start);
            end = start + HOST_PAGE_ALIGN(len);
            if (start > real_start)
                munmap((void *)real_start, start - real_start);
            if (end < real_end)
                munmap((void *)end, real_end - end);
            /* use it as a fixed mapping */
            flags |= MAP_FIXED;
        } else {
            /* if not fixed, no need to do anything */
            host_offset = offset & qemu_host_page_mask;
            host_len = len + offset - host_offset;
            host_start = (long)mmap(real_start ? g2h(real_start) : NULL,
                                    host_len, prot, flags, fd, host_offset);
            if (host_start == -1)
                return host_start;
            /* update start so that it points to the file position at 'offset' */
            if (!(flags & MAP_ANONYMOUS)) 
                host_start += offset - host_offset;
            start = h2g(host_start);
            goto the_end1;
        }
    }
    
    if (start & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        return -1;
    }
    end = start + len;
    real_end = HOST_PAGE_ALIGN(end);

    /* worst case: we cannot map the file because the offset is not
       aligned, so we read it */
    if (!(flags & MAP_ANONYMOUS) &&
        (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
        /* msync() won't work here, so we return an error if write is
           possible while it is a shared mapping */
        if ((flags & MAP_TYPE) == MAP_SHARED &&
            (prot & PROT_WRITE)) {
            errno = EINVAL;
            return -1;
        }
        retaddr = target_mmap(start, len, prot | PROT_WRITE, 
                              MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, 
                              -1, 0);
        if (retaddr == -1)
            return retaddr;
        pread(fd, g2h(start), len, offset);
        if (!(prot & PROT_WRITE)) {
            ret = target_mprotect(start, len, prot);
            if (ret != 0)
                return ret;
        }
        goto the_end;
    }

    /* handle the start of the mapping */
    if (start > real_start) {
        if (real_end == real_start + qemu_host_page_size) {
            /* one single host page */
            ret = mmap_frag(real_start, start, end,
                            prot, flags, fd, offset);
            if (ret == -1)
                return ret;
            goto the_end1;
        }
        ret = mmap_frag(real_start, start, real_start + qemu_host_page_size,
                        prot, flags, fd, offset);
        if (ret == -1)
            return ret;
        real_start += qemu_host_page_size;
    }
    /* handle the end of the mapping */
    if (end < real_end) {
        ret = mmap_frag(real_end - qemu_host_page_size, 
                        real_end - qemu_host_page_size, real_end,
                        prot, flags, fd, 
                        offset + real_end - qemu_host_page_size - start);
        if (ret == -1)
            return ret;
        real_end -= qemu_host_page_size;
    }
    
    /* map the middle (easier) */
    if (real_start < real_end) {
        unsigned long offset1;
	if (flags & MAP_ANONYMOUS)
	  offset1 = 0;
	else
	  offset1 = offset + real_start - start;
        ret = (long)mmap(g2h(real_start), real_end - real_start, 
                         prot, flags, fd, offset1);
        if (ret == -1)
            return ret;
    }
 the_end1:
    page_set_flags(start, start + len, prot | PAGE_VALID);
 the_end:
#ifdef DEBUG_MMAP
    printf("ret=0x%lx\n", (long)start);
    page_dump(stdout);
    printf("\n");
#endif
    return start;
}

int target_munmap(target_ulong start, target_ulong len)
{
    target_ulong end, real_start, real_end, addr;
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
    real_start = start & qemu_host_page_mask;
    real_end = HOST_PAGE_ALIGN(end);

    if (start > real_start) {
        /* handle host page containing start */
        prot = 0;
        for(addr = real_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (real_end == real_start + qemu_host_page_size) {
            for(addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
                prot |= page_get_flags(addr);
            }
            end = real_end;
        }
        if (prot != 0)
            real_start += qemu_host_page_size;
    }
    if (end < real_end) {
        prot = 0;
        for(addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (prot != 0)
            real_end -= qemu_host_page_size;
    }
    
    /* unmap what we can */
    if (real_start < real_end) {
        ret = munmap((void *)real_start, real_end - real_start);
        if (ret != 0)
            return ret;
    }

    page_set_flags(start, start + len, 0);
    return 0;
}

/* XXX: currently, we only handle MAP_ANONYMOUS and not MAP_FIXED
   blocks which have been allocated starting on a host page */
long target_mremap(target_ulong old_addr, target_ulong old_size, 
                   target_ulong new_size, unsigned long flags,
                   target_ulong new_addr)
{
    int prot;

    /* XXX: use 5 args syscall */
    new_addr = (long)mremap(g2h(old_addr), old_size, new_size, flags);
    if (new_addr == -1)
        return new_addr;
    new_addr = h2g(new_addr);
    prot = page_get_flags(old_addr);
    page_set_flags(old_addr, old_addr + old_size, 0);
    page_set_flags(new_addr, new_addr + new_size, prot | PAGE_VALID);
    return new_addr;
}

int target_msync(target_ulong start, target_ulong len, int flags)
{
    target_ulong end;

    if (start & ~TARGET_PAGE_MASK)
        return -EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    end = start + len;
    if (end < start)
        return -EINVAL;
    if (end == start)
        return 0;
    
    start &= qemu_host_page_mask;
    return msync(g2h(start), end - start, flags);
}

