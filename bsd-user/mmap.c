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

    qemu_log_mask(CPU_LOG_PAGE, "mprotect: start=0x" TARGET_ABI_FMT_lx
                  " len=0x" TARGET_ABI_FMT_lx " prot=%c%c%c\n", start, len,
                  prot & PROT_READ ? 'r' : '-',
                  prot & PROT_WRITE ? 'w' : '-',
                  prot & PROT_EXEC ? 'x' : '-');
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
        for (addr = host_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        if (host_end == host_start + qemu_host_page_size) {
            for (addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
                prot1 |= page_get_flags(addr);
            }
            end = host_end;
        }
        ret = mprotect(g2h_untagged(host_start),
                       qemu_host_page_size, prot1 & PAGE_BITS);
        if (ret != 0)
            goto error;
        host_start += qemu_host_page_size;
    }
    if (end < host_end) {
        prot1 = prot;
        for (addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        ret = mprotect(g2h_untagged(host_end - qemu_host_page_size),
                       qemu_host_page_size, prot1 & PAGE_BITS);
        if (ret != 0)
            goto error;
        host_end -= qemu_host_page_size;
    }

    /* handle the pages in the middle */
    if (host_start < host_end) {
        ret = mprotect(g2h_untagged(host_start), host_end - host_start, prot);
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

/*
 * map an incomplete host page
 *
 * mmap_frag can be called with a valid fd, if flags doesn't contain one of
 * MAP_ANON, MAP_STACK, MAP_GUARD. If we need to map a page in those cases, we
 * pass fd == -1. However, if flags contains MAP_GUARD then MAP_ANON cannot be
 * added.
 *
 * * If fd is valid (not -1) we want to map the pages with MAP_ANON.
 * * If flags contains MAP_GUARD we don't want to add MAP_ANON because it
 *   will be rejected.  See kern_mmap's enforcing of constraints for MAP_GUARD
 *   in sys/vm/vm_mmap.c.
 * * If flags contains MAP_ANON it doesn't matter if we add it or not.
 * * If flags contains MAP_STACK, mmap adds MAP_ANON when called so doesn't
 *   matter if we add it or not either. See enforcing of constraints for
 *   MAP_STACK in kern_mmap.
 *
 * Don't add MAP_ANON for the flags that use fd == -1 without specifying the
 * flags directly, with the assumption that future flags that require fd == -1
 * will also not require MAP_ANON.
 */
static int mmap_frag(abi_ulong real_start,
                     abi_ulong start, abi_ulong end,
                     int prot, int flags, int fd, abi_ulong offset)
{
    abi_ulong real_end, addr;
    void *host_start;
    int prot1, prot_new;

    real_end = real_start + qemu_host_page_size;
    host_start = g2h_untagged(real_start);

    /* get the protection of the target pages outside the mapping */
    prot1 = 0;
    for (addr = real_start; addr < real_end; addr++) {
        if (addr < start || addr >= end)
            prot1 |= page_get_flags(addr);
    }

    if (prot1 == 0) {
        /* no page was there, so we allocate one. See also above. */
        void *p = mmap(host_start, qemu_host_page_size, prot,
                       flags | ((fd != -1) ? MAP_ANON : 0), -1, 0);
        if (p == MAP_FAILED)
            return -1;
        prot1 = prot;
    }
    prot1 &= PAGE_BITS;

    prot_new = prot | prot1;
    if (fd != -1) {
        /* msync() won't work here, so we return an error if write is
           possible while it is a shared mapping */
        if ((flags & TARGET_BSD_MAP_FLAGMASK) == MAP_SHARED &&
            (prot & PROT_WRITE))
            return -1;

        /* adjust protection to be able to read */
        if (!(prot1 & PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot1 | PROT_WRITE);

        /* read the corresponding file data */
        if (pread(fd, g2h_untagged(start), end - start, offset) == -1) {
            return -1;
        }

        /* put final protection */
        if (prot_new != (prot1 | PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot_new);
    } else {
        if (prot_new != prot1) {
            mprotect(host_start, qemu_host_page_size, prot_new);
        }
        if (prot_new & PROT_WRITE) {
            memset(g2h_untagged(start), 0, end - start);
        }
    }
    return 0;
}

#if HOST_LONG_BITS == 64 && TARGET_ABI_BITS == 64
# define TASK_UNMAPPED_BASE  (1ul << 38)
#else
# define TASK_UNMAPPED_BASE  0x40000000
#endif
abi_ulong mmap_next_start = TASK_UNMAPPED_BASE;

unsigned long last_brk;

/*
 * Subroutine of mmap_find_vma, used when we have pre-allocated a chunk of guest
 * address space.
 */
static abi_ulong mmap_find_vma_reserved(abi_ulong start, abi_ulong size,
                                        abi_ulong alignment)
{
    abi_ulong addr;
    abi_ulong end_addr;
    int prot;
    int looped = 0;

    if (size > reserved_va) {
        return (abi_ulong)-1;
    }

    size = HOST_PAGE_ALIGN(size) + alignment;
    end_addr = start + size;
    if (end_addr > reserved_va) {
        end_addr = reserved_va;
    }
    addr = end_addr - qemu_host_page_size;

    while (1) {
        if (addr > end_addr) {
            if (looped) {
                return (abi_ulong)-1;
            }
            end_addr = reserved_va;
            addr = end_addr - qemu_host_page_size;
            looped = 1;
            continue;
        }
        prot = page_get_flags(addr);
        if (prot) {
            end_addr = addr;
        }
        if (end_addr - addr >= size) {
            break;
        }
        addr -= qemu_host_page_size;
    }

    if (start == mmap_next_start) {
        mmap_next_start = addr;
    }
    /* addr is sufficiently low to align it up */
    if (alignment != 0) {
        addr = (addr + alignment) & ~(alignment - 1);
    }
    return addr;
}

/*
 * Find and reserve a free memory area of size 'size'. The search
 * starts at 'start'.
 * It must be called with mmap_lock() held.
 * Return -1 if error.
 */
static abi_ulong mmap_find_vma_aligned(abi_ulong start, abi_ulong size,
                                       abi_ulong alignment)
{
    void *ptr, *prev;
    abi_ulong addr;
    int flags;
    int wrapped, repeat;

    /* If 'start' == 0, then a default start address is used. */
    if (start == 0) {
        start = mmap_next_start;
    } else {
        start &= qemu_host_page_mask;
    }

    size = HOST_PAGE_ALIGN(size);

    if (reserved_va) {
        return mmap_find_vma_reserved(start, size,
            (alignment != 0 ? 1 << alignment : 0));
    }

    addr = start;
    wrapped = repeat = 0;
    prev = 0;
    flags = MAP_ANON | MAP_PRIVATE;
    if (alignment != 0) {
        flags |= MAP_ALIGNED(alignment);
    }

    for (;; prev = ptr) {
        /*
         * Reserve needed memory area to avoid a race.
         * It should be discarded using:
         *  - mmap() with MAP_FIXED flag
         *  - mremap() with MREMAP_FIXED flag
         *  - shmat() with SHM_REMAP flag
         */
        ptr = mmap(g2h_untagged(addr), size, PROT_NONE,
                   flags, -1, 0);

        /* ENOMEM, if host address space has no memory */
        if (ptr == MAP_FAILED) {
            return (abi_ulong)-1;
        }

        /*
         * Count the number of sequential returns of the same address.
         * This is used to modify the search algorithm below.
         */
        repeat = (ptr == prev ? repeat + 1 : 0);

        if (h2g_valid(ptr + size - 1)) {
            addr = h2g(ptr);

            if ((addr & ~TARGET_PAGE_MASK) == 0) {
                /* Success.  */
                if (start == mmap_next_start && addr >= TASK_UNMAPPED_BASE) {
                    mmap_next_start = addr + size;
                }
                return addr;
            }

            /* The address is not properly aligned for the target.  */
            switch (repeat) {
            case 0:
                /*
                 * Assume the result that the kernel gave us is the
                 * first with enough free space, so start again at the
                 * next higher target page.
                 */
                addr = TARGET_PAGE_ALIGN(addr);
                break;
            case 1:
                /*
                 * Sometimes the kernel decides to perform the allocation
                 * at the top end of memory instead.
                 */
                addr &= TARGET_PAGE_MASK;
                break;
            case 2:
                /* Start over at low memory.  */
                addr = 0;
                break;
            default:
                /* Fail.  This unaligned block must the last.  */
                addr = -1;
                break;
            }
        } else {
            /*
             * Since the result the kernel gave didn't fit, start
             * again at low memory.  If any repetition, fail.
             */
            addr = (repeat ? -1 : 0);
        }

        /* Unmap and try again.  */
        munmap(ptr, size);

        /* ENOMEM if we checked the whole of the target address space.  */
        if (addr == (abi_ulong)-1) {
            return (abi_ulong)-1;
        } else if (addr == 0) {
            if (wrapped) {
                return (abi_ulong)-1;
            }
            wrapped = 1;
            /*
             * Don't actually use 0 when wrapping, instead indicate
             * that we'd truly like an allocation in low memory.
             */
            addr = TARGET_PAGE_SIZE;
        } else if (wrapped && addr >= start) {
            return (abi_ulong)-1;
        }
    }
}

abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size)
{
    return mmap_find_vma_aligned(start, size, 0);
}

/* NOTE: all the constants are the HOST ones */
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, off_t offset)
{
    abi_ulong ret, end, real_start, real_end, retaddr, host_offset, host_len;

    mmap_lock();
    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        qemu_log("mmap: start=0x" TARGET_ABI_FMT_lx
                 " len=0x" TARGET_ABI_FMT_lx " prot=%c%c%c flags=",
                 start, len,
                 prot & PROT_READ ? 'r' : '-',
                 prot & PROT_WRITE ? 'w' : '-',
                 prot & PROT_EXEC ? 'x' : '-');
        if (flags & MAP_ALIGNMENT_MASK) {
            qemu_log("MAP_ALIGNED(%u) ",
                     (flags & MAP_ALIGNMENT_MASK) >> MAP_ALIGNMENT_SHIFT);
        }
        if (flags & MAP_GUARD) {
            qemu_log("MAP_GUARD ");
        }
        if (flags & MAP_FIXED) {
            qemu_log("MAP_FIXED ");
        }
        if (flags & MAP_ANON) {
            qemu_log("MAP_ANON ");
        }
        if (flags & MAP_EXCL) {
            qemu_log("MAP_EXCL ");
        }
        if (flags & MAP_PRIVATE) {
            qemu_log("MAP_PRIVATE ");
        }
        if (flags & MAP_SHARED) {
            qemu_log("MAP_SHARED ");
        }
        if (flags & MAP_NOCORE) {
            qemu_log("MAP_NOCORE ");
        }
        if (flags & MAP_STACK) {
            qemu_log("MAP_STACK ");
        }
        qemu_log("fd=%d offset=0x%lx\n", fd, offset);
    }

    if ((flags & MAP_ANON) && fd != -1) {
        errno = EINVAL;
        goto fail;
    }
    if (flags & MAP_STACK) {
        if ((fd != -1) || ((prot & (PROT_READ | PROT_WRITE)) !=
                    (PROT_READ | PROT_WRITE))) {
            errno = EINVAL;
            goto fail;
        }
    }
    if ((flags & MAP_GUARD) && (prot != PROT_NONE || fd != -1 ||
        offset != 0 || (flags & (MAP_SHARED | MAP_PRIVATE |
        /* MAP_PREFAULT | */ /* MAP_PREFAULT not in mman.h */
        MAP_PREFAULT_READ | MAP_ANON | MAP_STACK)) != 0)) {
        errno = EINVAL;
        goto fail;
    }

    if (offset & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        goto fail;
    }

    if (len == 0) {
        errno = EINVAL;
        goto fail;
    }

    /* Check for overflows */
    len = TARGET_PAGE_ALIGN(len);
    if (len == 0) {
        errno = ENOMEM;
        goto fail;
    }

    real_start = start & qemu_host_page_mask;
    host_offset = offset & qemu_host_page_mask;

    /*
     * If the user is asking for the kernel to find a location, do that
     * before we truncate the length for mapping files below.
     */
    if (!(flags & MAP_FIXED)) {
        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);
        if ((flags & MAP_ALIGNMENT_MASK) != 0)
            start = mmap_find_vma_aligned(real_start, host_len,
                (flags & MAP_ALIGNMENT_MASK) >> MAP_ALIGNMENT_SHIFT);
        else
            start = mmap_find_vma(real_start, host_len);
        if (start == (abi_ulong)-1) {
            errno = ENOMEM;
            goto fail;
        }
    }

    /*
     * When mapping files into a memory area larger than the file, accesses
     * to pages beyond the file size will cause a SIGBUS.
     *
     * For example, if mmaping a file of 100 bytes on a host with 4K pages
     * emulating a target with 8K pages, the target expects to be able to
     * access the first 8K. But the host will trap us on any access beyond
     * 4K.
     *
     * When emulating a target with a larger page-size than the hosts, we
     * may need to truncate file maps at EOF and add extra anonymous pages
     * up to the targets page boundary.
     */

    if ((qemu_real_host_page_size() < qemu_host_page_size) && fd != -1) {
        struct stat sb;

        if (fstat(fd, &sb) == -1) {
            goto fail;
        }

        /* Are we trying to create a map beyond EOF?.  */
        if (offset + len > sb.st_size) {
            /*
             * If so, truncate the file map at eof aligned with
             * the hosts real pagesize. Additional anonymous maps
             * will be created beyond EOF.
             */
            len = REAL_HOST_PAGE_ALIGN(sb.st_size - offset);
        }
    }

    if (!(flags & MAP_FIXED)) {
        unsigned long host_start;
        void *p;

        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);

        /*
         * Note: we prefer to control the mapping address. It is
         * especially important if qemu_host_page_size >
         * qemu_real_host_page_size
         */
        p = mmap(g2h_untagged(start), host_len, prot,
                 flags | MAP_FIXED | ((fd != -1) ? MAP_ANON : 0), -1, 0);
        if (p == MAP_FAILED)
            goto fail;
        /* update start so that it points to the file position at 'offset' */
        host_start = (unsigned long)p;
        if (fd != -1) {
            p = mmap(g2h_untagged(start), len, prot,
                     flags | MAP_FIXED, fd, host_offset);
            if (p == MAP_FAILED) {
                munmap(g2h_untagged(start), host_len);
                goto fail;
            }
            host_start += offset - host_offset;
        }
        start = h2g(host_start);
    } else {
        if (start & ~TARGET_PAGE_MASK) {
            errno = EINVAL;
            goto fail;
        }
        end = start + len;
        real_end = HOST_PAGE_ALIGN(end);

        /*
         * Test if requested memory area fits target address space
         * It can fail only on 64-bit host with 32-bit target.
         * On any other target/host host mmap() handles this error correctly.
         */
        if (!guest_range_valid_untagged(start, len)) {
            errno = EINVAL;
            goto fail;
        }

        /*
         * worst case: we cannot map the file because the offset is not
         * aligned, so we read it
         */
        if (fd != -1 &&
            (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
            /*
             * msync() won't work here, so we return an error if write is
             * possible while it is a shared mapping
             */
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
            if (pread(fd, g2h_untagged(start), len, offset) == -1) {
                goto fail;
            }
            if (!(prot & PROT_WRITE)) {
                ret = target_mprotect(start, len, prot);
                assert(ret == 0);
            }
            goto the_end;
        }

        /* Reject the mapping if any page within the range is mapped */
        if ((flags & MAP_EXCL) && page_check_range(start, len, 0) < 0) {
            errno = EINVAL;
            goto fail;
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
                            real_end - qemu_host_page_size, end,
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
            p = mmap(g2h_untagged(real_start), real_end - real_start,
                     prot, flags, fd, offset1);
            if (p == MAP_FAILED)
                goto fail;
        }
    }
 the_end1:
    page_set_flags(start, start + len, prot | PAGE_VALID);
 the_end:
#ifdef DEBUG_MMAP
    printf("ret=0x" TARGET_ABI_FMT_lx "\n", start);
    page_dump(stdout);
    printf("\n");
#endif
    mmap_unlock();
    return start;
fail:
    mmap_unlock();
    return -1;
}

static void mmap_reserve(abi_ulong start, abi_ulong size)
{
    abi_ulong real_start;
    abi_ulong real_end;
    abi_ulong addr;
    abi_ulong end;
    int prot;

    real_start = start & qemu_host_page_mask;
    real_end = HOST_PAGE_ALIGN(start + size);
    end = start + size;
    if (start > real_start) {
        /* handle host page containing start */
        prot = 0;
        for (addr = real_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (real_end == real_start + qemu_host_page_size) {
            for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
                prot |= page_get_flags(addr);
            }
            end = real_end;
        }
        if (prot != 0) {
            real_start += qemu_host_page_size;
        }
    }
    if (end < real_end) {
        prot = 0;
        for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (prot != 0) {
            real_end -= qemu_host_page_size;
        }
    }
    if (real_start != real_end) {
        mmap(g2h_untagged(real_start), real_end - real_start, PROT_NONE,
                 MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
    }
}

int target_munmap(abi_ulong start, abi_ulong len)
{
    abi_ulong end, real_start, real_end, addr;
    int prot, ret;

#ifdef DEBUG_MMAP
    printf("munmap: start=0x" TARGET_ABI_FMT_lx " len=0x"
           TARGET_ABI_FMT_lx "\n",
           start, len);
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
        for (addr = real_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (real_end == real_start + qemu_host_page_size) {
            for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
                prot |= page_get_flags(addr);
            }
            end = real_end;
        }
        if (prot != 0)
            real_start += qemu_host_page_size;
    }
    if (end < real_end) {
        prot = 0;
        for (addr = end; addr < real_end; addr += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(addr);
        }
        if (prot != 0)
            real_end -= qemu_host_page_size;
    }

    ret = 0;
    /* unmap what we can */
    if (real_start < real_end) {
        if (reserved_va) {
            mmap_reserve(real_start, real_end - real_start);
        } else {
            ret = munmap(g2h_untagged(real_start), real_end - real_start);
        }
    }

    if (ret == 0) {
        page_set_flags(start, start + len, 0);
    }
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
    return msync(g2h_untagged(start), end - start, flags);
}
