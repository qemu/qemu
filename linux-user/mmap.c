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
#include <sys/shm.h>
#include "trace.h"
#include "exec/log.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"
#include "target_mman.h"
#include "qemu/interval-tree.h"

#ifdef TARGET_ARM
#include "target/arm/cpu-features.h"
#endif

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
    assert(mmap_lock_count > 0);
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
    if (child) {
        pthread_mutex_init(&mmap_mutex, NULL);
    } else {
        pthread_mutex_unlock(&mmap_mutex);
    }
}

/* Protected by mmap_lock. */
static IntervalTreeRoot shm_regions;

static void shm_region_add(abi_ptr start, abi_ptr last)
{
    IntervalTreeNode *i = g_new0(IntervalTreeNode, 1);

    i->start = start;
    i->last = last;
    interval_tree_insert(i, &shm_regions);
}

static abi_ptr shm_region_find(abi_ptr start)
{
    IntervalTreeNode *i;

    for (i = interval_tree_iter_first(&shm_regions, start, start); i;
         i = interval_tree_iter_next(i, start, start)) {
        if (i->start == start) {
            return i->last;
        }
    }
    return 0;
}

static void shm_region_rm_complete(abi_ptr start, abi_ptr last)
{
    IntervalTreeNode *i, *n;

    for (i = interval_tree_iter_first(&shm_regions, start, last); i; i = n) {
        n = interval_tree_iter_next(i, start, last);
        if (i->start >= start && i->last <= last) {
            interval_tree_remove(i, &shm_regions);
            g_free(i);
        }
    }
}

/*
 * Validate target prot bitmask.
 * Return the prot bitmask for the host in *HOST_PROT.
 * Return 0 if the target prot bitmask is invalid, otherwise
 * the internal qemu page_flags (which will include PAGE_VALID).
 */
static int validate_prot_to_pageflags(int prot)
{
    int valid = PROT_READ | PROT_WRITE | PROT_EXEC | TARGET_PROT_SEM;
    int page_flags = (prot & PAGE_BITS) | PAGE_VALID;

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
#elif defined(TARGET_HPPA)
    valid |= PROT_GROWSDOWN | PROT_GROWSUP;
#endif

    return prot & ~valid ? 0 : page_flags;
}

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
static int target_to_host_prot(int prot)
{
    return (prot & (PROT_READ | PROT_WRITE)) |
           (prot & PROT_EXEC ? PROT_READ : 0);
}

/* NOTE: all the constants are the HOST ones, but addresses are target. */
int target_mprotect(abi_ulong start, abi_ulong len, int target_prot)
{
    abi_ulong starts[3];
    abi_ulong lens[3];
    int prots[3];
    abi_ulong host_start, host_last, last;
    int prot1, ret, page_flags, nranges;

    trace_target_mprotect(start, len, target_prot);

    if ((start & ~TARGET_PAGE_MASK) != 0) {
        return -TARGET_EINVAL;
    }
    page_flags = validate_prot_to_pageflags(target_prot);
    if (!page_flags) {
        return -TARGET_EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    len = TARGET_PAGE_ALIGN(len);
    if (!guest_range_valid_untagged(start, len)) {
        return -TARGET_ENOMEM;
    }

    last = start + len - 1;
    host_start = start & qemu_host_page_mask;
    host_last = HOST_PAGE_ALIGN(last) - 1;
    nranges = 0;

    mmap_lock();

    if (host_last - host_start < qemu_host_page_size) {
        /* Single host page contains all guest pages: sum the prot. */
        prot1 = target_prot;
        for (abi_ulong a = host_start; a < start; a += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(a);
        }
        for (abi_ulong a = last; a < host_last; a += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(a + 1);
        }
        starts[nranges] = host_start;
        lens[nranges] = qemu_host_page_size;
        prots[nranges] = prot1;
        nranges++;
    } else {
        if (host_start < start) {
            /* Host page contains more than one guest page: sum the prot. */
            prot1 = target_prot;
            for (abi_ulong a = host_start; a < start; a += TARGET_PAGE_SIZE) {
                prot1 |= page_get_flags(a);
            }
            /* If the resulting sum differs, create a new range. */
            if (prot1 != target_prot) {
                starts[nranges] = host_start;
                lens[nranges] = qemu_host_page_size;
                prots[nranges] = prot1;
                nranges++;
                host_start += qemu_host_page_size;
            }
        }

        if (last < host_last) {
            /* Host page contains more than one guest page: sum the prot. */
            prot1 = target_prot;
            for (abi_ulong a = last; a < host_last; a += TARGET_PAGE_SIZE) {
                prot1 |= page_get_flags(a + 1);
            }
            /* If the resulting sum differs, create a new range. */
            if (prot1 != target_prot) {
                host_last -= qemu_host_page_size;
                starts[nranges] = host_last + 1;
                lens[nranges] = qemu_host_page_size;
                prots[nranges] = prot1;
                nranges++;
            }
        }

        /* Create a range for the middle, if any remains. */
        if (host_start < host_last) {
            starts[nranges] = host_start;
            lens[nranges] = host_last - host_start + 1;
            prots[nranges] = target_prot;
            nranges++;
        }
    }

    for (int i = 0; i < nranges; ++i) {
        ret = mprotect(g2h_untagged(starts[i]), lens[i],
                       target_to_host_prot(prots[i]));
        if (ret != 0) {
            goto error;
        }
    }

    page_set_flags(start, last, page_flags);
    ret = 0;

 error:
    mmap_unlock();
    return ret;
}

/* map an incomplete host page */
static bool mmap_frag(abi_ulong real_start, abi_ulong start, abi_ulong last,
                      int prot, int flags, int fd, off_t offset)
{
    abi_ulong real_last;
    void *host_start;
    int prot_old, prot_new;
    int host_prot_old, host_prot_new;

    if (!(flags & MAP_ANONYMOUS)
        && (flags & MAP_TYPE) == MAP_SHARED
        && (prot & PROT_WRITE)) {
        /*
         * msync() won't work with the partial page, so we return an
         * error if write is possible while it is a shared mapping.
         */
        errno = EINVAL;
        return false;
    }

    real_last = real_start + qemu_host_page_size - 1;
    host_start = g2h_untagged(real_start);

    /* Get the protection of the target pages outside the mapping. */
    prot_old = 0;
    for (abi_ulong a = real_start; a < start; a += TARGET_PAGE_SIZE) {
        prot_old |= page_get_flags(a);
    }
    for (abi_ulong a = real_last; a > last; a -= TARGET_PAGE_SIZE) {
        prot_old |= page_get_flags(a);
    }

    if (prot_old == 0) {
        /*
         * Since !(prot_old & PAGE_VALID), there were no guest pages
         * outside of the fragment we need to map.  Allocate a new host
         * page to cover, discarding whatever else may have been present.
         */
        void *p = mmap(host_start, qemu_host_page_size,
                       target_to_host_prot(prot),
                       flags | MAP_ANONYMOUS, -1, 0);
        if (p != host_start) {
            if (p != MAP_FAILED) {
                munmap(p, qemu_host_page_size);
                errno = EEXIST;
            }
            return false;
        }
        prot_old = prot;
    }
    prot_new = prot | prot_old;

    host_prot_old = target_to_host_prot(prot_old);
    host_prot_new = target_to_host_prot(prot_new);

    /* Adjust protection to be able to write. */
    if (!(host_prot_old & PROT_WRITE)) {
        host_prot_old |= PROT_WRITE;
        mprotect(host_start, qemu_host_page_size, host_prot_old);
    }

    /* Read or zero the new guest pages. */
    if (flags & MAP_ANONYMOUS) {
        memset(g2h_untagged(start), 0, last - start + 1);
    } else {
        if (pread(fd, g2h_untagged(start), last - start + 1, offset) == -1) {
            return false;
        }
    }

    /* Put final protection */
    if (host_prot_new != host_prot_old) {
        mprotect(host_start, qemu_host_page_size, host_prot_new);
    }
    return true;
}

abi_ulong task_unmapped_base;
abi_ulong elf_et_dyn_base;
abi_ulong mmap_next_start;

/*
 * Subroutine of mmap_find_vma, used when we have pre-allocated
 * a chunk of guest address space.
 */
static abi_ulong mmap_find_vma_reserved(abi_ulong start, abi_ulong size,
                                        abi_ulong align)
{
    target_ulong ret;

    ret = page_find_range_empty(start, reserved_va, size, align);
    if (ret == -1 && start > mmap_min_addr) {
        /* Restart at the beginning of the address space. */
        ret = page_find_range_empty(mmap_min_addr, start - 1, size, align);
    }

    return ret;
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
                   MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);

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

            if ((addr & (align - 1)) == 0) {
                /* Success.  */
                if (start == mmap_next_start && addr >= task_unmapped_base) {
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
                addr = ROUND_UP(addr, align);
                break;
            case 1:
                /*
                 * Sometimes the kernel decides to perform the allocation
                 * at the top end of memory instead.
                 */
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
                     int flags, int fd, off_t offset)
{
    abi_ulong ret, last, real_start, real_last, retaddr, host_len;
    abi_ulong passthrough_start = -1, passthrough_last = 0;
    int page_flags;
    off_t host_offset;

    mmap_lock();
    trace_target_mmap(start, len, target_prot, flags, fd, offset);

    if (!len) {
        errno = EINVAL;
        goto fail;
    }

    page_flags = validate_prot_to_pageflags(target_prot);
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

    /*
     * If the user is asking for the kernel to find a location, do that
     * before we truncate the length for mapping files below.
     */
    if (!(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);
        start = mmap_find_vma(real_start, host_len, TARGET_PAGE_SIZE);
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
    if ((qemu_real_host_page_size() < qemu_host_page_size) &&
        !(flags & MAP_ANONYMOUS)) {
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

    if (!(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        uintptr_t host_start;
        int host_prot;
        void *p;

        host_len = len + offset - host_offset;
        host_len = HOST_PAGE_ALIGN(host_len);
        host_prot = target_to_host_prot(target_prot);

        /*
         * Note: we prefer to control the mapping address. It is
         * especially important if qemu_host_page_size >
         * qemu_real_host_page_size.
         */
        p = mmap(g2h_untagged(start), host_len, host_prot,
                 flags | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            goto fail;
        }
        /* update start so that it points to the file position at 'offset' */
        host_start = (uintptr_t)p;
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
        last = start + len - 1;
        passthrough_start = start;
        passthrough_last = last;
    } else {
        if (start & ~TARGET_PAGE_MASK) {
            errno = EINVAL;
            goto fail;
        }
        last = start + len - 1;
        real_last = HOST_PAGE_ALIGN(last) - 1;

        /*
         * Test if requested memory area fits target address space
         * It can fail only on 64-bit host with 32-bit target.
         * On any other target/host host mmap() handles this error correctly.
         */
        if (last < start || !guest_range_valid_untagged(start, len)) {
            errno = ENOMEM;
            goto fail;
        }

        if (flags & MAP_FIXED_NOREPLACE) {
            /* Validate that the chosen range is empty. */
            if (!page_check_range_empty(start, last)) {
                errno = EEXIST;
                goto fail;
            }

            /*
             * With reserved_va, the entire address space is mmaped in the
             * host to ensure it isn't accidentally used for something else.
             * We have just checked that the guest address is not mapped
             * within the guest, but need to replace the host reservation.
             *
             * Without reserved_va, despite the guest address check above,
             * keep MAP_FIXED_NOREPLACE so that the guest does not overwrite
             * any host address mappings.
             */
            if (reserved_va) {
                flags = (flags & ~MAP_FIXED_NOREPLACE) | MAP_FIXED;
            }
        }

        /*
         * worst case: we cannot map the file because the offset is not
         * aligned, so we read it
         */
        if (!(flags & MAP_ANONYMOUS) &&
            (offset & ~qemu_host_page_mask) != (start & ~qemu_host_page_mask)) {
            /*
             * msync() won't work here, so we return an error if write is
             * possible while it is a shared mapping
             */
            if ((flags & MAP_TYPE) == MAP_SHARED
                && (target_prot & PROT_WRITE)) {
                errno = EINVAL;
                goto fail;
            }
            retaddr = target_mmap(start, len, target_prot | PROT_WRITE,
                                  (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))
                                  | MAP_PRIVATE | MAP_ANONYMOUS,
                                  -1, 0);
            if (retaddr == -1) {
                goto fail;
            }
            if (pread(fd, g2h_untagged(start), len, offset) == -1) {
                goto fail;
            }
            if (!(target_prot & PROT_WRITE)) {
                ret = target_mprotect(start, len, target_prot);
                assert(ret == 0);
            }
            goto the_end;
        }

        /* handle the start of the mapping */
        if (start > real_start) {
            if (real_last == real_start + qemu_host_page_size - 1) {
                /* one single host page */
                if (!mmap_frag(real_start, start, last,
                               target_prot, flags, fd, offset)) {
                    goto fail;
                }
                goto the_end1;
            }
            if (!mmap_frag(real_start, start,
                           real_start + qemu_host_page_size - 1,
                           target_prot, flags, fd, offset)) {
                goto fail;
            }
            real_start += qemu_host_page_size;
        }
        /* handle the end of the mapping */
        if (last < real_last) {
            abi_ulong real_page = real_last - qemu_host_page_size + 1;
            if (!mmap_frag(real_page, real_page, last,
                           target_prot, flags, fd,
                           offset + real_page - start)) {
                goto fail;
            }
            real_last -= qemu_host_page_size;
        }

        /* map the middle (easier) */
        if (real_start < real_last) {
            void *p, *want_p;
            off_t offset1;
            size_t len1;

            if (flags & MAP_ANONYMOUS) {
                offset1 = 0;
            } else {
                offset1 = offset + real_start - start;
            }
            len1 = real_last - real_start + 1;
            want_p = g2h_untagged(real_start);

            p = mmap(want_p, len1, target_to_host_prot(target_prot),
                     flags, fd, offset1);
            if (p != want_p) {
                if (p != MAP_FAILED) {
                    munmap(p, len1);
                    errno = EEXIST;
                }
                goto fail;
            }
            passthrough_start = real_start;
            passthrough_last = real_last;
        }
    }
 the_end1:
    if (flags & MAP_ANONYMOUS) {
        page_flags |= PAGE_ANON;
    }
    page_flags |= PAGE_RESET;
    if (passthrough_start > passthrough_last) {
        page_set_flags(start, last, page_flags);
    } else {
        if (start < passthrough_start) {
            page_set_flags(start, passthrough_start - 1, page_flags);
        }
        page_set_flags(passthrough_start, passthrough_last,
                       page_flags | PAGE_PASSTHROUGH);
        if (passthrough_last < last) {
            page_set_flags(passthrough_last + 1, last, page_flags);
        }
    }
    shm_region_rm_complete(start, last);
 the_end:
    trace_target_mmap_complete(start);
    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        FILE *f = qemu_log_trylock();
        if (f) {
            fprintf(f, "page layout changed following mmap\n");
            page_dump(f);
            qemu_log_unlock(f);
        }
    }
    mmap_unlock();
    return start;
fail:
    mmap_unlock();
    return -1;
}

static int mmap_reserve_or_unmap(abi_ulong start, abi_ulong len)
{
    abi_ulong real_start;
    abi_ulong real_last;
    abi_ulong real_len;
    abi_ulong last;
    abi_ulong a;
    void *host_start;
    int prot;

    last = start + len - 1;
    real_start = start & qemu_host_page_mask;
    real_last = HOST_PAGE_ALIGN(last) - 1;

    /*
     * If guest pages remain on the first or last host pages,
     * adjust the deallocation to retain those guest pages.
     * The single page special case is required for the last page,
     * lest real_start overflow to zero.
     */
    if (real_last - real_start < qemu_host_page_size) {
        prot = 0;
        for (a = real_start; a < start; a += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(a);
        }
        for (a = last; a < real_last; a += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(a + 1);
        }
        if (prot != 0) {
            return 0;
        }
    } else {
        for (prot = 0, a = real_start; a < start; a += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(a);
        }
        if (prot != 0) {
            real_start += qemu_host_page_size;
        }

        for (prot = 0, a = last; a < real_last; a += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(a + 1);
        }
        if (prot != 0) {
            real_last -= qemu_host_page_size;
        }

        if (real_last < real_start) {
            return 0;
        }
    }

    real_len = real_last - real_start + 1;
    host_start = g2h_untagged(real_start);

    if (reserved_va) {
        void *ptr = mmap(host_start, real_len, PROT_NONE,
                         MAP_FIXED | MAP_ANONYMOUS
                         | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
        return ptr == host_start ? 0 : -1;
    }
    return munmap(host_start, real_len);
}

int target_munmap(abi_ulong start, abi_ulong len)
{
    int ret;

    trace_target_munmap(start, len);

    if (start & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        return -1;
    }
    len = TARGET_PAGE_ALIGN(len);
    if (len == 0 || !guest_range_valid_untagged(start, len)) {
        errno = EINVAL;
        return -1;
    }

    mmap_lock();
    ret = mmap_reserve_or_unmap(start, len);
    if (likely(ret == 0)) {
        page_set_flags(start, start + len - 1, 0);
        shm_region_rm_complete(start, start + len - 1);
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
            /*
             * If new and old addresses overlap then the above mremap will
             * already have failed with EINVAL.
             */
            mmap_reserve_or_unmap(old_addr, old_size);
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
                mmap_reserve_or_unmap(old_addr, old_size);
            }
        }
    } else {
        int page_flags = 0;
        if (reserved_va && old_size < new_size) {
            abi_ulong addr;
            for (addr = old_addr + old_size;
                 addr < old_addr + new_size;
                 addr++) {
                page_flags |= page_get_flags(addr);
            }
        }
        if (page_flags == 0) {
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
                    mmap_reserve_or_unmap(old_addr + old_size,
                                          old_size - new_size);
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
        page_set_flags(old_addr, old_addr + old_size - 1, 0);
        shm_region_rm_complete(old_addr, old_addr + old_size - 1);
        page_set_flags(new_addr, new_addr + new_size - 1,
                       prot | PAGE_VALID | PAGE_RESET);
        shm_region_rm_complete(new_addr, new_addr + new_size - 1);
    }
    mmap_unlock();
    return new_addr;
}

abi_long target_madvise(abi_ulong start, abi_ulong len_in, int advice)
{
    abi_ulong len;
    int ret = 0;

    if (start & ~TARGET_PAGE_MASK) {
        return -TARGET_EINVAL;
    }
    if (len_in == 0) {
        return 0;
    }
    len = TARGET_PAGE_ALIGN(len_in);
    if (len == 0 || !guest_range_valid_untagged(start, len)) {
        return -TARGET_EINVAL;
    }

    /* Translate for some architectures which have different MADV_xxx values */
    switch (advice) {
    case TARGET_MADV_DONTNEED:      /* alpha */
        advice = MADV_DONTNEED;
        break;
    case TARGET_MADV_WIPEONFORK:    /* parisc */
        advice = MADV_WIPEONFORK;
        break;
    case TARGET_MADV_KEEPONFORK:    /* parisc */
        advice = MADV_KEEPONFORK;
        break;
    /* we do not care about the other MADV_xxx values yet */
    }

    /*
     * Most advice values are hints, so ignoring and returning success is ok.
     *
     * However, some advice values such as MADV_DONTNEED, MADV_WIPEONFORK and
     * MADV_KEEPONFORK are not hints and need to be emulated.
     *
     * A straight passthrough for those may not be safe because qemu sometimes
     * turns private file-backed mappings into anonymous mappings.
     * If all guest pages have PAGE_PASSTHROUGH set, mappings have the
     * same semantics for the host as for the guest.
     *
     * We pass through MADV_WIPEONFORK and MADV_KEEPONFORK if possible and
     * return failure if not.
     *
     * MADV_DONTNEED is passed through as well, if possible.
     * If passthrough isn't possible, we nevertheless (wrongly!) return
     * success, which is broken but some userspace programs fail to work
     * otherwise. Completely implementing such emulation is quite complicated
     * though.
     */
    mmap_lock();
    switch (advice) {
    case MADV_WIPEONFORK:
    case MADV_KEEPONFORK:
        ret = -EINVAL;
        /* fall through */
    case MADV_DONTNEED:
        if (page_check_range(start, len, PAGE_PASSTHROUGH)) {
            ret = get_errno(madvise(g2h_untagged(start), len, advice));
            if ((advice == MADV_DONTNEED) && (ret == 0)) {
                page_reset_target_data(start, start + len - 1);
            }
        }
    }
    mmap_unlock();

    return ret;
}

#ifndef TARGET_FORCE_SHMLBA
/*
 * For most architectures, SHMLBA is the same as the page size;
 * some architectures have larger values, in which case they should
 * define TARGET_FORCE_SHMLBA and provide a target_shmlba() function.
 * This corresponds to the kernel arch code defining __ARCH_FORCE_SHMLBA
 * and defining its own value for SHMLBA.
 *
 * The kernel also permits SHMLBA to be set by the architecture to a
 * value larger than the page size without setting __ARCH_FORCE_SHMLBA;
 * this means that addresses are rounded to the large size if
 * SHM_RND is set but addresses not aligned to that size are not rejected
 * as long as they are at least page-aligned. Since the only architecture
 * which uses this is ia64 this code doesn't provide for that oddity.
 */
static inline abi_ulong target_shmlba(CPUArchState *cpu_env)
{
    return TARGET_PAGE_SIZE;
}
#endif

abi_ulong target_shmat(CPUArchState *cpu_env, int shmid,
                       abi_ulong shmaddr, int shmflg)
{
    CPUState *cpu = env_cpu(cpu_env);
    abi_ulong raddr;
    struct shmid_ds shm_info;
    int ret;
    abi_ulong shmlba;

    /* shmat pointers are always untagged */

    /* find out the length of the shared memory segment */
    ret = get_errno(shmctl(shmid, IPC_STAT, &shm_info));
    if (is_error(ret)) {
        /* can't get length, bail out */
        return ret;
    }

    shmlba = target_shmlba(cpu_env);

    if (shmaddr & (shmlba - 1)) {
        if (shmflg & SHM_RND) {
            shmaddr &= ~(shmlba - 1);
        } else {
            return -TARGET_EINVAL;
        }
    }
    if (!guest_range_valid_untagged(shmaddr, shm_info.shm_segsz)) {
        return -TARGET_EINVAL;
    }

    WITH_MMAP_LOCK_GUARD() {
        void *host_raddr;
        abi_ulong last;

        if (shmaddr) {
            host_raddr = shmat(shmid, (void *)g2h_untagged(shmaddr), shmflg);
        } else {
            abi_ulong mmap_start;

            /* In order to use the host shmat, we need to honor host SHMLBA.  */
            mmap_start = mmap_find_vma(0, shm_info.shm_segsz,
                                       MAX(SHMLBA, shmlba));

            if (mmap_start == -1) {
                return -TARGET_ENOMEM;
            }
            host_raddr = shmat(shmid, g2h_untagged(mmap_start),
                               shmflg | SHM_REMAP);
        }

        if (host_raddr == (void *)-1) {
            return get_errno(-1);
        }
        raddr = h2g(host_raddr);
        last = raddr + shm_info.shm_segsz - 1;

        page_set_flags(raddr, last,
                       PAGE_VALID | PAGE_RESET | PAGE_READ |
                       (shmflg & SHM_RDONLY ? 0 : PAGE_WRITE));

        shm_region_rm_complete(raddr, last);
        shm_region_add(raddr, last);
    }

    /*
     * We're mapping shared memory, so ensure we generate code for parallel
     * execution and flush old translations.  This will work up to the level
     * supported by the host -- anything that requires EXCP_ATOMIC will not
     * be atomic with respect to an external process.
     */
    if (!(cpu->tcg_cflags & CF_PARALLEL)) {
        cpu->tcg_cflags |= CF_PARALLEL;
        tb_flush(cpu);
    }

    return raddr;
}

abi_long target_shmdt(abi_ulong shmaddr)
{
    abi_long rv;

    /* shmdt pointers are always untagged */

    WITH_MMAP_LOCK_GUARD() {
        abi_ulong last = shm_region_find(shmaddr);
        if (last == 0) {
            return -TARGET_EINVAL;
        }

        rv = get_errno(shmdt(g2h_untagged(shmaddr)));
        if (rv == 0) {
            abi_ulong size = last - shmaddr + 1;

            page_set_flags(shmaddr, last, 0);
            shm_region_rm_complete(shmaddr, last);
            mmap_reserve_or_unmap(shmaddr, size);
        }
    }
    return rv;
}
