/*
 *  mmap support for qemu
 *
 *  Copyright (c) 2003 - 2008 Fabrice Bellard
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"

#include "qemu.h"
#include "qemu-common.h"
#include "bsd-mman.h"

//#define DEBUG_MMAP

static pthread_mutex_t mmap_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int mmap_lock_count;

void mmap_lock(void)
{
    if (mmap_lock_count++ == 0) {
        pthread_mutex_lock(&mmap_mutex);
    }
}

void mmap_unlock(void)
{
    if (--mmap_lock_count == 0) {
        pthread_mutex_unlock(&mmap_mutex);
    }
}

bool have_mmap_lock(void)
{
    return mmap_lock_count > 0 ? true : false;
}

/* Grab lock to make sure things are in a consistent state after fork().  */
void mmap_fork_start(void)
{
    if (mmap_lock_count)
        abort();
    pthread_mutex_lock(&mmap_mutex);
}

void mmap_fork_end(int child)
{
    if (child)
        pthread_mutex_init(&mmap_mutex, NULL);
    else
        pthread_mutex_unlock(&mmap_mutex);
}

/* NOTE: all the constants are the HOST ones, but addresses are target. */
int target_mprotect(abi_ulong start, abi_ulong len, int prot)
{
    abi_ulong end, host_start, host_end, addr;
    int prot1, ret;

#ifdef DEBUG_MMAP
    printf("mprotect: start=0x" TARGET_FMT_lx
           " len=0x" TARGET_FMT_lx " prot=%c%c%c\n", start, len,
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
    prot &= PROT_READ | PROT_WRITE | PROT_EXEC;
    if (len == 0)
        return 0;

    mmap_lock();
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
            goto error;
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
            goto error;
        host_end -= qemu_host_page_size;
    }

    /* handle the pages in the middle */
    if (host_start < host_end) {
        ret = mprotect(g2h(host_start), host_end - host_start, prot);
        if (ret != 0)
            goto error;
    }
    page_set_flags(start, start + len, prot | PAGE_VALID);
    mmap_unlock();
    return 0;
error:
    mmap_unlock();
    return ret;
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
                       flags | MAP_ANON, -1, 0);
        if (p == MAP_FAILED)
            return -1;
        prot1 = prot;
    }
    prot1 &= PAGE_BITS;

    prot_new = prot | prot1;
    if (!(flags & MAP_ANON)) {
        /* msync() won't work here, so we return an error if write is
           possible while it is a shared mapping */
        if ((flags & TARGET_BSD_MAP_FLAGMASK) == MAP_SHARED &&
            (prot & PROT_WRITE))
            return -1;

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

static abi_ulong mmap_next_start = 0x40000000;

unsigned long last_brk;

/* find a free memory area of size 'size'. The search starts at
   'start'. If 'start' == 0, then a default start address is used.
   Return -1 if error.
*/
/* page_init() marks pages used by the host as reserved to be sure not
   to use them. */
static abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size)
{
    abi_ulong addr, addr1, addr_start;
    int prot;
    unsigned long new_brk;

    new_brk = (unsigned long)sbrk(0);
    if (last_brk && last_brk < new_brk && last_brk == (target_ulong)last_brk) {
        /* This is a hack to catch the host allocating memory with brk().
           If it uses mmap then we loose.
           FIXME: We really want to avoid the host allocating memory in
           the first place, and maybe leave some slack to avoid switching
           to mmap.  */
        page_set_flags(last_brk & TARGET_PAGE_MASK,
                       TARGET_PAGE_ALIGN(new_brk),
                       PAGE_RESERVED);
    }
    last_brk = new_brk;

    size = HOST_PAGE_ALIGN(size);
    start = start & qemu_host_page_mask;
    addr = start;
    if (addr == 0)
        addr = mmap_next_start;
    addr_start = addr;
    for(;;) {
        prot = 0;
        for(addr1 = addr; addr1 < (addr + size); addr1 += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr1);
        }
        if (prot == 0)
            break;
        addr += qemu_host_page_size;
        /* we found nothing */
        if (addr == addr_start)
            return (abi_ulong)-1;
    }
    if (start == 0)
        mmap_next_start = addr + size;
    return addr;
}

/* NOTE: all the constants are the HOST ones */
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, abi_ulong offset)
{
    abi_ulong ret, end, real_start, real_end, retaddr, host_offset, host_len;
    unsigned long host_start;

    mmap_lock();
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
        if (flags & MAP_ANON)
            printf("MAP_ANON ");
        switch(flags & TARGET_BSD_MAP_FLAGMASK) {
        case MAP_PRIVATE:
            printf("MAP_PRIVATE ");
            break;
        case MAP_SHARED:
            printf("MAP_SHARED ");
            break;
        default:
            printf("[MAP_FLAGMASK=0x%x] ", flags & TARGET_BSD_MAP_FLAGMASK);
            break;
        }
        printf("fd=%d offset=" TARGET_FMT_lx "\n", fd, offset);
    }
#endif

    if (offset & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        goto fail;
    }

    len = TARGET_PAGE_ALIGN(len);
    if (len == 0)
        goto the_end;
    real_start = start & qemu_host_page_mask;

    if (!(flags & MAP_FIXED)) {
        abi_ulong mmap_start;
        void *p;
        host_offset = offset & qemu_host_page_mask;
        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);
        mmap_start = mmap_find_vma(real_start, host_len);
        if (mmap_start == (abi_ulong)-1) {
            errno = ENOMEM;
            goto fail;
        }
        /* Note: we prefer to control the mapping address. It is
           especially important if qemu_host_page_size >
           qemu_real_host_page_size */
        p = mmap(g2h(mmap_start),
                 host_len, prot, flags | MAP_FIXED, fd, host_offset);
        if (p == MAP_FAILED)
            goto fail;
        /* update start so that it points to the file position at 'offset' */
        host_start = (unsigned long)p;
        if (!(flags & MAP_ANON))
            host_start += offset - host_offset;
        start = h2g(host_start);
    } else {
        int flg;
        target_ulong addr;

        if (start & ~TARGET_PAGE_MASK) {
            errno = EINVAL;
            goto fail;
        }
        end = start + len;
        real_end = HOST_PAGE_ALIGN(end);

        for(addr = real_start; addr < real_end; addr += TARGET_PAGE_SIZE) {
            flg = page_get_flags(addr);
            if (flg & PAGE_RESERVED) {
                errno = ENXIO;
                goto fail;
            }
        }

        /* worst case: we cannot map the file because the offset is not
           aligned, so we read it */
        if (!(flags & MAP_ANON) &&
            (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
            /* msync() won't work here, so we return an error if write is
               possible while it is a shared mapping */
            if ((flags & TARGET_BSD_MAP_FLAGMASK) == MAP_SHARED &&
                (prot & PROT_WRITE)) {
                errno = EINVAL;
                goto fail;
            }
            retaddr = target_mmap(start, len, prot | PROT_WRITE,
                                  MAP_FIXED | MAP_PRIVATE | MAP_ANON,
                                  -1, 0);
            if (retaddr == -1)
                goto fail;
            pread(fd, g2h(start), len, offset);
            if (!(prot & PROT_WRITE)) {
                ret = target_mprotect(start, len, prot);
                if (ret != 0) {
                    start = ret;
                    goto the_end;
                }
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
                    goto fail;
                goto the_end1;
            }
            ret = mmap_frag(real_start, start, real_start + qemu_host_page_size,
                            prot, flags, fd, offset);
            if (ret == -1)
                goto fail;
            real_start += qemu_host_page_size;
        }
        /* handle the end of the mapping */
        if (end < real_end) {
            ret = mmap_frag(real_end - qemu_host_page_size,
                            real_end - qemu_host_page_size, real_end,
                            prot, flags, fd,
                            offset + real_end - qemu_host_page_size - start);
            if (ret == -1)
                goto fail;
            real_end -= qemu_host_page_size;
        }

        /* map the middle (easier) */
        if (real_start < real_end) {
            void *p;
            unsigned long offset1;
            if (flags & MAP_ANON)
                offset1 = 0;
            else
                offset1 = offset + real_start - start;
            p = mmap(g2h(real_start), real_end - real_start,
                     prot, flags, fd, offset1);
            if (p == MAP_FAILED)
                goto fail;
        }
    }
 the_end1:
    page_set_flags(start, start + len, prot | PAGE_VALID);
 the_end:
#ifdef DEBUG_MMAP
    printf("ret=0x" TARGET_FMT_lx "\n", start);
    page_dump(stdout);
    printf("\n");
#endif
    mmap_unlock();
    return start;
fail:
    mmap_unlock();
    return -1;
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
    mmap_lock();
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

    ret = 0;
    /* unmap what we can */
    if (real_start < real_end) {
        ret = munmap(g2h(real_start), real_end - real_start);
    }

    if (ret == 0)
        page_set_flags(start, start + len, 0);
    mmap_unlock();
    return ret;
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
