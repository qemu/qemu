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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "trace.h"
#include "exec/log.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"

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

/*
 * Validate target prot bitmask.
 * Return the prot bitmask for the host in *HOST_PROT.
 * Return 0 if the target prot bitmask is invalid, otherwise
 * the internal qemu page_flags (which will include PAGE_VALID).
 */
static int validate_prot_to_pageflags(int *host_prot, int prot)
{
    int valid = PROT_READ | PROT_WRITE | PROT_EXEC | TARGET_PROT_SEM;
    int page_flags = (prot & PAGE_BITS) | PAGE_VALID;

    /*
     * For the host, we need not pass anything except read/write/exec.
     * While PROT_SEM is allowed by all hosts, it is also ignored, so
     * don't bother transforming guest bit to host bit.  Any other
     * target-specific prot bits will not be understood by the host
     * and will need to be encoded into page_flags for qemu emulation.
     *
     * Pages that are executable by the guest will never be executed
     * by the host, but the host will need to be able to read them.
     */
    *host_prot = (prot & (PROT_READ | PROT_WRITE))
               | (prot & PROT_EXEC ? PROT_READ : 0);

#ifdef TARGET_AARCH64
    {
        ARMCPU *cpu = ARM_CPU(thread_cpu);

        /*
         * The PROT_BTI bit is only accepted if the cpu supports the feature.
         * Since this is the unusual case, don't bother checking unless
         * the bit has been requested.  If set and valid, record the bit
         * within QEMU's page_flags.
         */
        if ((prot & TARGET_PROT_BTI) && cpu_isar_feature(aa64_bti, cpu)) {
            valid |= TARGET_PROT_BTI;
            page_flags |= PAGE_BTI;
        }
        /* Similarly for the PROT_MTE bit. */
        if ((prot & TARGET_PROT_MTE) && cpu_isar_feature(aa64_mte, cpu)) {
            valid |= TARGET_PROT_MTE;
            page_flags |= PAGE_MTE;
        }
    }
#endif

    return prot & ~valid ? 0 : page_flags;
}

/* NOTE: all the constants are the HOST ones, but addresses are target. */
int target_mprotect(abi_ulong start, abi_ulong len, int target_prot)
{
    abi_ulong end, host_start, host_end, addr;
    int prot1, ret, page_flags, host_prot;

    trace_target_mprotect(start, len, target_prot);

    if ((start & ~TARGET_PAGE_MASK) != 0) {
        return -TARGET_EINVAL;
    }
    page_flags = validate_prot_to_pageflags(&host_prot, target_prot);
    if (!page_flags) {
        return -TARGET_EINVAL;
    }
    len = TARGET_PAGE_ALIGN(len);
    end = start + len;
    if (!guest_range_valid_untagged(start, len)) {
        return -TARGET_ENOMEM;
    }
    if (len == 0) {
        return 0;
    }

    mmap_lock();
    host_start = start & qemu_host_page_mask;
    host_end = HOST_PAGE_ALIGN(end);
    if (start > host_start) {
        /* handle host page containing start */
        prot1 = host_prot;
        for (addr = host_start; addr < start; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        if (host_end == host_start + qemu_host_page_size) {
            for (addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
                prot1 |= page_get_flags(addr);
            }
            end = host_end;
        }
        ret = mprotect(g2h_untagged(host_start), qemu_host_page_size,
                       prot1 & PAGE_BITS);
        if (ret != 0) {
            goto error;
        }
        host_start += qemu_host_page_size;
    }
    if (end < host_end) {
        prot1 = host_prot;
        for (addr = end; addr < host_end; addr += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(addr);
        }
        ret = mprotect(g2h_untagged(host_end - qemu_host_page_size),
                       qemu_host_page_size, prot1 & PAGE_BITS);
        if (ret != 0) {
            goto error;
        }
        host_end -= qemu_host_page_size;
    }

    /* handle the pages in the middle */
    if (host_start < host_end) {
        ret = mprotect(g2h_untagged(host_start),
                       host_end - host_start, host_prot);
        if (ret != 0) {
            goto error;
        }
    }
    page_set_flags(start, start + len, page_flags);
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
    host_start = g2h_untagged(real_start);

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
            return -1;

        /* adjust protection to be able to read */
        if (!(prot1 & PROT_WRITE))
            mprotect(host_start, qemu_host_page_size, prot1 | PROT_WRITE);

        /* read the corresponding file data */
        if (pread(fd, g2h_untagged(start), end - start, offset) == -1)
            return -1;

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
#ifdef TARGET_AARCH64
# define TASK_UNMAPPED_BASE  0x5500000000
#else
# define TASK_UNMAPPED_BASE  (1ul << 38)
#endif
#else
# define TASK_UNMAPPED_BASE  0x40000000
#endif
abi_ulong mmap_next_start = TASK_UNMAPPED_BASE;

unsigned long last_brk;

/* Subroutine of mmap_find_vma, used when we have pre-allocated a chunk
   of guest address space.  */
static abi_ulong mmap_find_vma_reserved(abi_ulong start, abi_ulong size,
                                        abi_ulong align)
{
    abi_ulong addr, end_addr, incr = qemu_host_page_size;
    int prot;
    bool looped = false;

    if (size > reserved_va) {
        return (abi_ulong)-1;
    }

    /* Note that start and size have already been aligned by mmap_find_vma. */

    end_addr = start + size;
    if (start > reserved_va - size) {
        /* Start at the top of the address space.  */
        end_addr = ((reserved_va - size) & -align) + size;
        looped = true;
    }

    /* Search downward from END_ADDR, checking to see if a page is in use.  */
    addr = end_addr;
    while (1) {
        addr -= incr;
        if (addr > end_addr) {
            if (looped) {
                /* Failure.  The entire address space has been searched.  */
                return (abi_ulong)-1;
            }
            /* Re-start at the top of the address space.  */
            addr = end_addr = ((reserved_va - size) & -align) + size;
            looped = true;
        } else {
            prot = page_get_flags(addr);
            if (prot) {
                /* Page in use.  Restart below this page.  */
                addr = end_addr = ((addr - size) & -align) + size;
            } else if (addr && addr + size == end_addr) {
                /* Success!  All pages between ADDR and END_ADDR are free.  */
                if (start == mmap_next_start) {
                    mmap_next_start = addr;
                }
                return addr;
            }
        }
    }
}

/*
 * Find and reserve a free memory area of size 'size'. The search
 * starts at 'start'.
 * It must be called with mmap_lock() held.
 * Return -1 if error.
 */
abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size, abi_ulong align)
{
    void *ptr, *prev;
    abi_ulong addr;
    int wrapped, repeat;

    align = MAX(align, qemu_host_page_size);

    /* If 'start' == 0, then a default start address is used. */
    if (start == 0) {
        start = mmap_next_start;
    } else {
        start &= qemu_host_page_mask;
    }
    start = ROUND_UP(start, align);

    size = HOST_PAGE_ALIGN(size);

    if (reserved_va) {
        return mmap_find_vma_reserved(start, size, align);
    }

    addr = start;
    wrapped = repeat = 0;
    prev = 0;

    for (;; prev = ptr) {
        /*
         * Reserve needed memory area to avoid a race.
         * It should be discarded using:
         *  - mmap() with MAP_FIXED flag
         *  - mremap() with MREMAP_FIXED flag
         *  - shmat() with SHM_REMAP flag
         */
        ptr = mmap(g2h_untagged(addr), size, PROT_NONE,
                   MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE, -1, 0);

        /* ENOMEM, if host address space has no memory */
        if (ptr == MAP_FAILED) {
            return (abi_ulong)-1;
        }

        /* Count the number of sequential returns of the same address.
           This is used to modify the search algorithm below.  */
        repeat = (ptr == prev ? repeat + 1 : 0);

        if (h2g_valid(ptr + size - 1)) {
            addr = h2g(ptr);

            if ((addr & (align - 1)) == 0) {
                /* Success.  */
                if (start == mmap_next_start && addr >= TASK_UNMAPPED_BASE) {
                    mmap_next_start = addr + size;
                }
                return addr;
            }

            /* The address is not properly aligned for the target.  */
            switch (repeat) {
            case 0:
                /* Assume the result that the kernel gave us is the
                   first with enough free space, so start again at the
                   next higher target page.  */
                addr = ROUND_UP(addr, align);
                break;
            case 1:
                /* Sometimes the kernel decides to perform the allocation
                   at the top end of memory instead.  */
                addr &= -align;
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
            /* Since the result the kernel gave didn't fit, start
               again at low memory.  If any repetition, fail.  */
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
            /* Don't actually use 0 when wrapping, instead indicate
               that we'd truly like an allocation in low memory.  */
            addr = (mmap_min_addr > TARGET_PAGE_SIZE
                     ? TARGET_PAGE_ALIGN(mmap_min_addr)
                     : TARGET_PAGE_SIZE);
        } else if (wrapped && addr >= start) {
            return (abi_ulong)-1;
        }
    }
}

/* NOTE: all the constants are the HOST ones */
abi_long target_mmap(abi_ulong start, abi_ulong len, int target_prot,
                     int flags, int fd, abi_ulong offset)
{
    abi_ulong ret, end, real_start, real_end, retaddr, host_offset, host_len;
    int page_flags, host_prot;

    mmap_lock();
    trace_target_mmap(start, len, target_prot, flags, fd, offset);

    if (!len) {
        errno = EINVAL;
        goto fail;
    }

    page_flags = validate_prot_to_pageflags(&host_prot, target_prot);
    if (!page_flags) {
        errno = EINVAL;
        goto fail;
    }

    /* Also check for overflows... */
    len = TARGET_PAGE_ALIGN(len);
    if (!len) {
        errno = ENOMEM;
        goto fail;
    }

    if (offset & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        goto fail;
    }

    /*
     * If we're mapping shared memory, ensure we generate code for parallel
     * execution and flush old translations.  This will work up to the level
     * supported by the host -- anything that requires EXCP_ATOMIC will not
     * be atomic with respect to an external process.
     */
    if (flags & MAP_SHARED) {
        CPUState *cpu = thread_cpu;
        if (!(cpu->tcg_cflags & CF_PARALLEL)) {
            cpu->tcg_cflags |= CF_PARALLEL;
            tb_flush(cpu);
        }
    }

    real_start = start & qemu_host_page_mask;
    host_offset = offset & qemu_host_page_mask;

    /* If the user is asking for the kernel to find a location, do that
       before we truncate the length for mapping files below.  */
    if (!(flags & MAP_FIXED)) {
        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);
        start = mmap_find_vma(real_start, host_len, TARGET_PAGE_SIZE);
        if (start == (abi_ulong)-1) {
            errno = ENOMEM;
            goto fail;
        }
    }

    /* When mapping files into a memory area larger than the file, accesses
       to pages beyond the file size will cause a SIGBUS. 

       For example, if mmaping a file of 100 bytes on a host with 4K pages
       emulating a target with 8K pages, the target expects to be able to
       access the first 8K. But the host will trap us on any access beyond
       4K.  

       When emulating a target with a larger page-size than the hosts, we
       may need to truncate file maps at EOF and add extra anonymous pages
       up to the targets page boundary.  */

    if ((qemu_real_host_page_size < qemu_host_page_size) &&
        !(flags & MAP_ANONYMOUS)) {
        struct stat sb;

       if (fstat (fd, &sb) == -1)
           goto fail;

       /* Are we trying to create a map beyond EOF?.  */
       if (offset + len > sb.st_size) {
           /* If so, truncate the file map at eof aligned with 
              the hosts real pagesize. Additional anonymous maps
              will be created beyond EOF.  */
           len = REAL_HOST_PAGE_ALIGN(sb.st_size - offset);
       }
    }

    if (!(flags & MAP_FIXED)) {
        unsigned long host_start;
        void *p;

        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);

        /* Note: we prefer to control the mapping address. It is
           especially important if qemu_host_page_size >
           qemu_real_host_page_size */
        p = mmap(g2h_untagged(start), host_len, host_prot,
                 flags | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            goto fail;
        }
        /* update start so that it points to the file position at 'offset' */
        host_start = (unsigned long)p;
        if (!(flags & MAP_ANONYMOUS)) {
            p = mmap(g2h_untagged(start), len, host_prot,
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
        if (end < start || !guest_range_valid_untagged(start, len)) {
            errno = ENOMEM;
            goto fail;
        }

        /* worst case: we cannot map the file because the offset is not
           aligned, so we read it */
        if (!(flags & MAP_ANONYMOUS) &&
            (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
            /* msync() won't work here, so we return an error if write is
               possible while it is a shared mapping */
            if ((flags & MAP_TYPE) == MAP_SHARED &&
                (host_prot & PROT_WRITE)) {
                errno = EINVAL;
                goto fail;
            }
            retaddr = target_mmap(start, len, target_prot | PROT_WRITE,
                                  MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                                  -1, 0);
            if (retaddr == -1)
                goto fail;
            if (pread(fd, g2h_untagged(start), len, offset) == -1)
                goto fail;
            if (!(host_prot & PROT_WRITE)) {
                ret = target_mprotect(start, len, target_prot);
                assert(ret == 0);
            }
            goto the_end;
        }
        
        /* handle the start of the mapping */
        if (start > real_start) {
            if (real_end == real_start + qemu_host_page_size) {
                /* one single host page */
                ret = mmap_frag(real_start, start, end,
                                host_prot, flags, fd, offset);
                if (ret == -1)
                    goto fail;
                goto the_end1;
            }
            ret = mmap_frag(real_start, start, real_start + qemu_host_page_size,
                            host_prot, flags, fd, offset);
            if (ret == -1)
                goto fail;
            real_start += qemu_host_page_size;
        }
        /* handle the end of the mapping */
        if (end < real_end) {
            ret = mmap_frag(real_end - qemu_host_page_size,
                            real_end - qemu_host_page_size, end,
                            host_prot, flags, fd,
                            offset + real_end - qemu_host_page_size - start);
            if (ret == -1)
                goto fail;
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
            p = mmap(g2h_untagged(real_start), real_end - real_start,
                     host_prot, flags, fd, offset1);
            if (p == MAP_FAILED)
                goto fail;
        }
    }
 the_end1:
    if (flags & MAP_ANONYMOUS) {
        page_flags |= PAGE_ANON;
    }
    page_flags |= PAGE_RESET;
    page_set_flags(start, start + len, page_flags);
 the_end:
    trace_target_mmap_complete(start);
    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        log_page_dump(__func__);
    }
    tb_invalidate_phys_range(start, start + len);
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
    if (real_start != real_end) {
        mmap(g2h_untagged(real_start), real_end - real_start, PROT_NONE,
                 MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE,
                 -1, 0);
    }
}

int target_munmap(abi_ulong start, abi_ulong len)
{
    abi_ulong end, real_start, real_end, addr;
    int prot, ret;

    trace_target_munmap(start, len);

    if (start & ~TARGET_PAGE_MASK)
        return -TARGET_EINVAL;
    len = TARGET_PAGE_ALIGN(len);
    if (len == 0 || !guest_range_valid_untagged(start, len)) {
        return -TARGET_EINVAL;
    }

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
        if (reserved_va) {
            mmap_reserve(real_start, real_end - real_start);
        } else {
            ret = munmap(g2h_untagged(real_start), real_end - real_start);
        }
    }

    if (ret == 0) {
        page_set_flags(start, start + len, 0);
        tb_invalidate_phys_range(start, start + len);
    }
    mmap_unlock();
    return ret;
}

abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr)
{
    int prot;
    void *host_addr;

    if (!guest_range_valid_untagged(old_addr, old_size) ||
        ((flags & MREMAP_FIXED) &&
         !guest_range_valid_untagged(new_addr, new_size)) ||
        ((flags & MREMAP_MAYMOVE) == 0 &&
         !guest_range_valid_untagged(old_addr, new_size))) {
        errno = ENOMEM;
        return -1;
    }

    mmap_lock();

    if (flags & MREMAP_FIXED) {
        host_addr = mremap(g2h_untagged(old_addr), old_size, new_size,
                           flags, g2h_untagged(new_addr));

        if (reserved_va && host_addr != MAP_FAILED) {
            /* If new and old addresses overlap then the above mremap will
               already have failed with EINVAL.  */
            mmap_reserve(old_addr, old_size);
        }
    } else if (flags & MREMAP_MAYMOVE) {
        abi_ulong mmap_start;

        mmap_start = mmap_find_vma(0, new_size, TARGET_PAGE_SIZE);

        if (mmap_start == -1) {
            errno = ENOMEM;
            host_addr = MAP_FAILED;
        } else {
            host_addr = mremap(g2h_untagged(old_addr), old_size, new_size,
                               flags | MREMAP_FIXED,
                               g2h_untagged(mmap_start));
            if (reserved_va) {
                mmap_reserve(old_addr, old_size);
            }
        }
    } else {
        int prot = 0;
        if (reserved_va && old_size < new_size) {
            abi_ulong addr;
            for (addr = old_addr + old_size;
                 addr < old_addr + new_size;
                 addr++) {
                prot |= page_get_flags(addr);
            }
        }
        if (prot == 0) {
            host_addr = mremap(g2h_untagged(old_addr),
                               old_size, new_size, flags);

            if (host_addr != MAP_FAILED) {
                /* Check if address fits target address space */
                if (!guest_range_valid_untagged(h2g(host_addr), new_size)) {
                    /* Revert mremap() changes */
                    host_addr = mremap(g2h_untagged(old_addr),
                                       new_size, old_size, flags);
                    errno = ENOMEM;
                    host_addr = MAP_FAILED;
                } else if (reserved_va && old_size > new_size) {
                    mmap_reserve(old_addr + old_size, old_size - new_size);
                }
            }
        } else {
            errno = ENOMEM;
            host_addr = MAP_FAILED;
        }
    }

    if (host_addr == MAP_FAILED) {
        new_addr = -1;
    } else {
        new_addr = h2g(host_addr);
        prot = page_get_flags(old_addr);
        page_set_flags(old_addr, old_addr + old_size, 0);
        page_set_flags(new_addr, new_addr + new_size,
                       prot | PAGE_VALID | PAGE_RESET);
    }
    tb_invalidate_phys_range(new_addr, new_addr + new_size);
    mmap_unlock();
    return new_addr;
}
