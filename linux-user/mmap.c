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
int target_mprotect(abi_ulong start, abi_ulong len, int prot)
{
    abi_ulong end, host_start, host_end, addr;
    int prot1, ret;

#ifdef DEBUG_MMAP
    printf("mprotect: start=0x" TARGET_FMT_lx
           "len=0x" TARGET_FMT_lx " prot=%c%c%c\n", start, len,
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
static int mmap_frag(abi_ulong real_start,
                     abi_ulong start, abi_ulong end,
                     int prot, int flags, int fd, abi_ulong offset)
{
    abi_ulong real_end, addr;
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
        void *p = mmap(host_start, qemu_host_page_size, prot,
                       flags | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            return -1;
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
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, abi_ulong offset)
{
    abi_ulong ret, end, real_start, real_end, retaddr, host_offset, host_len;
    unsigned long host_start;
#if defined(__alpha__) || defined(__sparc__) || defined(__x86_64__) || \
        defined(__ia64) || defined(__mips__)
    static abi_ulong last_start = 0x40000000;
#elif defined(__CYGWIN__)
    /* Cygwin doesn't have a whole lot of address space.  */
    static abi_ulong last_start = 0x18000000;
#endif

#ifdef DEBUG_MMAP
    {
        printf("mmap: start=0x" TARGET_FMT_lx
               " len=0x" TARGET_FMT_lx " prot=%c%c%c flags=",
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
        printf("fd=%d offset=" TARGET_FMT_lx "\n", fd, offset);
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
    defined(__ia64) || defined(__mips__) || defined(__CYGWIN__)
        /* tell the kernel to search at the same place as i386 */
        if (real_start == 0) {
            real_start = last_start;
            last_start += HOST_PAGE_ALIGN(len);
        }
#endif
            host_offset = offset & qemu_host_page_mask;
            host_len = len + offset - host_offset;

        if (qemu_host_page_size > qemu_real_host_page_size) {
            /*
             * The guest expects to see mmapped areas aligned to it's pagesize.
             * If the host's real page size is smaller than the guest's, we need
             * to fixup the maps. It is done by allocating a larger area,
             * displacing the map (if needed) and finally chopping off the spare
             * room at the edges.
             */

            /*
             * We assume qemu_host_page_size is always the same as
             * TARGET_PAGE_SIZE, see exec.c. qemu_real_host_page_size is the
             * hosts real page size.
             */
            abi_ulong host_end;
            unsigned long host_aligned_start;
            void *p;

            host_len = HOST_PAGE_ALIGN(host_len + qemu_host_page_size
                                       - qemu_real_host_page_size);
            p = mmap(real_start ? g2h(real_start) : NULL,
                     host_len, prot, flags, fd, host_offset);
            if (p == MAP_FAILED)
                return -1;

            host_start = (unsigned long)p;
            host_end = host_start + host_len;

            /* Find start and end, aligned to the targets pagesize with-in the
               large mmaped area.  */
            host_aligned_start = TARGET_PAGE_ALIGN(host_start);
            if (!(flags & MAP_ANONYMOUS))
                host_aligned_start += offset - host_offset;

            start = h2g(host_aligned_start);
            end = start + TARGET_PAGE_ALIGN(len);

            /* Chop off the leftovers, if any.  */
            if (host_aligned_start > host_start)
                munmap((void *)host_start, host_aligned_start - host_start);
            if (end < host_end)
                munmap((void *)g2h(end), host_end - end);

            goto the_end1;
        } else {
            /* if not fixed, no need to do anything */
            void *p = mmap(real_start ? g2h(real_start) : NULL,
                                    host_len, prot, flags, fd, host_offset);
            if (p == MAP_FAILED)
                return -1;
            /* update start so that it points to the file position at 'offset' */
            host_start = (unsigned long)p;
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
            return -1;
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
            return -1;
        real_end -= qemu_host_page_size;
    }

    /* map the middle (easier) */
    if (real_start < real_end) {
        void *p;
        unsigned long offset1;
        if (flags & MAP_ANONYMOUS)
          offset1 = 0;
        else
          offset1 = offset + real_start - start;
        p = mmap(g2h(real_start), real_end - real_start,
                 prot, flags, fd, offset1);
        if (p == MAP_FAILED)
            return -1;
    }
 the_end1:
    page_set_flags(start, start + len, prot | PAGE_VALID);
 the_end:
#ifdef DEBUG_MMAP
    printf("ret=0x%llx\n", start);
    page_dump(stdout);
    printf("\n");
#endif
    return start;
}

int target_munmap(abi_ulong start, abi_ulong len)
{
    abi_ulong end, real_start, real_end, addr;
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
        ret = munmap(g2h(real_start), real_end - real_start);
        if (ret != 0)
            return ret;
    }

    page_set_flags(start, start + len, 0);
    return 0;
}

/* XXX: currently, we only handle MAP_ANONYMOUS and not MAP_FIXED
   blocks which have been allocated starting on a host page */
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr)
{
    int prot;
    unsigned long host_addr;

    /* XXX: use 5 args syscall */
    host_addr = (long)mremap(g2h(old_addr), old_size, new_size, flags);
    if (host_addr == -1)
        return -1;
    new_addr = h2g(host_addr);
    prot = page_get_flags(old_addr);
    page_set_flags(old_addr, old_addr + old_size, 0);
    page_set_flags(new_addr, new_addr + new_size, prot | PAGE_VALID);
    return new_addr;
}

int target_msync(abi_ulong start, abi_ulong len, int flags)
{
    abi_ulong end;

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

