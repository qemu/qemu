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
    int host_page_size = qemu_real_host_page_size();
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
    host_start = start & -host_page_size;
    host_last = ROUND_UP(last, host_page_size) - 1;
    nranges = 0;

    mmap_lock();

    if (host_last - host_start < host_page_size) {
        /* Single host page contains all guest pages: sum the prot. */
        prot1 = target_prot;
        for (abi_ulong a = host_start; a < start; a += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(a);
        }
        for (abi_ulong a = last; a < host_last; a += TARGET_PAGE_SIZE) {
            prot1 |= page_get_flags(a + 1);
        }
        starts[nranges] = host_start;
        lens[nranges] = host_page_size;
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
                lens[nranges] = host_page_size;
                prots[nranges] = prot1;
                nranges++;
                host_start += host_page_size;
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
                host_last -= host_page_size;
                starts[nranges] = host_last + 1;
                lens[nranges] = host_page_size;
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

/*
 * Perform munmap on behalf of the target, with host parameters.
 * If reserved_va, we must replace the memory reservation.
 */
static int do_munmap(void *addr, size_t len)
{
    if (reserved_va) {
        void *ptr = mmap(addr, len, PROT_NONE,
                         MAP_FIXED | MAP_ANONYMOUS
                         | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
        return ptr == addr ? 0 : -1;
    }
    return munmap(addr, len);
}

/*
 * Map an incomplete host page.
 *
 * Here be dragons.  This case will not work if there is an existing
 * overlapping host page, which is file mapped, and for which the mapping
 * is beyond the end of the file.  In that case, we will see SIGBUS when
 * trying to write a portion of this page.
 *
 * FIXME: Work around this with a temporary signal handler and longjmp.
 */
static bool mmap_frag(abi_ulong real_start, abi_ulong start, abi_ulong last,
                      int prot, int flags, int fd, off_t offset)
{
    int host_page_size = qemu_real_host_page_size();
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

    real_last = real_start + host_page_size - 1;
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
        void *p = mmap(host_start, host_page_size,
                       target_to_host_prot(prot),
                       flags | MAP_ANONYMOUS, -1, 0);
        if (p != host_start) {
            if (p != MAP_FAILED) {
                do_munmap(p, host_page_size);
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
        mprotect(host_start, host_page_size, host_prot_old);
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
        mprotect(host_start, host_page_size, host_prot_new);
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
    int host_page_size = qemu_real_host_page_size();
    void *ptr, *prev;
    abi_ulong addr;
    int wrapped, repeat;

    align = MAX(align, host_page_size);

    /* If 'start' == 0, then a default start address is used. */
    if (start == 0) {
        start = mmap_next_start;
    } else {
        start &= -host_page_size;
    }
    start = ROUND_UP(start, align);
    size = ROUND_UP(size, host_page_size);

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

/*
 * Record a successful mmap within the user-exec interval tree.
 */
static abi_long mmap_end(abi_ulong start, abi_ulong last,
                         abi_ulong passthrough_start,
                         abi_ulong passthrough_last,
                         int flags, int page_flags)
{
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
    trace_target_mmap_complete(start);
    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        FILE *f = qemu_log_trylock();
        if (f) {
            fprintf(f, "page layout changed following mmap\n");
            page_dump(f);
            qemu_log_unlock(f);
        }
    }
    return start;
}

/*
 * Special case host page size == target page size,
 * where there are no edge conditions.
 */
static abi_long mmap_h_eq_g(abi_ulong start, abi_ulong len,
                            int host_prot, int flags, int page_flags,
                            int fd, off_t offset)
{
    void *p, *want_p = g2h_untagged(start);
    abi_ulong last;

    p = mmap(want_p, len, host_prot, flags, fd, offset);
    if (p == MAP_FAILED) {
        return -1;
    }
    /* If the host kernel does not support MAP_FIXED_NOREPLACE, emulate. */
    if ((flags & MAP_FIXED_NOREPLACE) && p != want_p) {
        do_munmap(p, len);
        errno = EEXIST;
        return -1;
    }

    start = h2g(p);
    last = start + len - 1;
    return mmap_end(start, last, start, last, flags, page_flags);
}

/*
 * Special case host page size < target page size.
 *
 * The two special cases are increased guest alignment, and mapping
 * past the end of a file.
 *
 * When mapping files into a memory area larger than the file,
 * accesses to pages beyond the file size will cause a SIGBUS.
 *
 * For example, if mmaping a file of 100 bytes on a host with 4K
 * pages emulating a target with 8K pages, the target expects to
 * be able to access the first 8K. But the host will trap us on
 * any access beyond 4K.
 *
 * When emulating a target with a larger page-size than the hosts,
 * we may need to truncate file maps at EOF and add extra anonymous
 * pages up to the targets page boundary.
 *
 * This workaround only works for files that do not change.
 * If the file is later extended (e.g. ftruncate), the SIGBUS
 * vanishes and the proper behaviour is that changes within the
 * anon page should be reflected in the file.
 *
 * However, this case is rather common with executable images,
 * so the workaround is important for even trivial tests, whereas
 * the mmap of of a file being extended is less common.
 */
static abi_long mmap_h_lt_g(abi_ulong start, abi_ulong len, int host_prot,
                            int mmap_flags, int page_flags, int fd,
                            off_t offset, int host_page_size)
{
    void *p, *want_p = g2h_untagged(start);
    off_t fileend_adj = 0;
    int flags = mmap_flags;
    abi_ulong last, pass_last;

    if (!(flags & MAP_ANONYMOUS)) {
        struct stat sb;

        if (fstat(fd, &sb) == -1) {
            return -1;
        }
        if (offset >= sb.st_size) {
            /*
             * The entire map is beyond the end of the file.
             * Transform it to an anonymous mapping.
             */
            flags |= MAP_ANONYMOUS;
            fd = -1;
            offset = 0;
        } else if (offset + len > sb.st_size) {
            /*
             * A portion of the map is beyond the end of the file.
             * Truncate the file portion of the allocation.
             */
            fileend_adj = offset + len - sb.st_size;
        }
    }

    if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
        if (fileend_adj) {
            p = mmap(want_p, len, host_prot, flags | MAP_ANONYMOUS, -1, 0);
        } else {
            p = mmap(want_p, len, host_prot, flags, fd, offset);
        }
        if (p != want_p) {
            if (p != MAP_FAILED) {
                /* Host does not support MAP_FIXED_NOREPLACE: emulate. */
                do_munmap(p, len);
                errno = EEXIST;
            }
            return -1;
        }

        if (fileend_adj) {
            void *t = mmap(p, len - fileend_adj, host_prot,
                           (flags & ~MAP_FIXED_NOREPLACE) | MAP_FIXED,
                           fd, offset);

            if (t == MAP_FAILED) {
                int save_errno = errno;

                /*
                 * We failed a map over the top of the successful anonymous
                 * mapping above. The only failure mode is running out of VMAs,
                 * and there's nothing that we can do to detect that earlier.
                 * If we have replaced an existing mapping with MAP_FIXED,
                 * then we cannot properly recover.  It's a coin toss whether
                 * it would be better to exit or continue here.
                 */
                if (!(flags & MAP_FIXED_NOREPLACE) &&
                    !page_check_range_empty(start, start + len - 1)) {
                    qemu_log("QEMU target_mmap late failure: %s",
                             strerror(save_errno));
                }

                do_munmap(want_p, len);
                errno = save_errno;
                return -1;
            }
        }
    } else {
        size_t host_len, part_len;

        /*
         * Take care to align the host memory.  Perform a larger anonymous
         * allocation and extract the aligned portion.  Remap the file on
         * top of that.
         */
        host_len = len + TARGET_PAGE_SIZE - host_page_size;
        p = mmap(want_p, host_len, host_prot, flags | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            return -1;
        }

        part_len = (uintptr_t)p & (TARGET_PAGE_SIZE - 1);
        if (part_len) {
            part_len = TARGET_PAGE_SIZE - part_len;
            do_munmap(p, part_len);
            p += part_len;
            host_len -= part_len;
        }
        if (len < host_len) {
            do_munmap(p + len, host_len - len);
        }

        if (!(flags & MAP_ANONYMOUS)) {
            void *t = mmap(p, len - fileend_adj, host_prot,
                           flags | MAP_FIXED, fd, offset);

            if (t == MAP_FAILED) {
                int save_errno = errno;
                do_munmap(p, len);
                errno = save_errno;
                return -1;
            }
        }

        start = h2g(p);
    }

    last = start + len - 1;
    if (fileend_adj) {
        pass_last = ROUND_UP(last - fileend_adj, host_page_size) - 1;
    } else {
        pass_last = last;
    }
    return mmap_end(start, last, start, pass_last, mmap_flags, page_flags);
}

/*
 * Special case host page size > target page size.
 *
 * The two special cases are address and file offsets that are valid
 * for the guest that cannot be directly represented by the host.
 */
static abi_long mmap_h_gt_g(abi_ulong start, abi_ulong len,
                            int target_prot, int host_prot,
                            int flags, int page_flags, int fd,
                            off_t offset, int host_page_size)
{
    void *p, *want_p = g2h_untagged(start);
    off_t host_offset = offset & -host_page_size;
    abi_ulong last, real_start, real_last;
    bool misaligned_offset = false;
    size_t host_len;

    if (!(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        /*
         * Adjust the offset to something representable on the host.
         */
        host_len = len + offset - host_offset;
        p = mmap(want_p, host_len, host_prot, flags, fd, host_offset);
        if (p == MAP_FAILED) {
            return -1;
        }

        /* Update start to the file position at offset. */
        p += offset - host_offset;

        start = h2g(p);
        last = start + len - 1;
        return mmap_end(start, last, start, last, flags, page_flags);
    }

    if (!(flags & MAP_ANONYMOUS)) {
        misaligned_offset = (start ^ offset) & (host_page_size - 1);

        /*
         * The fallback for misalignment is a private mapping + read.
         * This carries none of semantics required of MAP_SHARED.
         */
        if (misaligned_offset && (flags & MAP_TYPE) != MAP_PRIVATE) {
            errno = EINVAL;
            return -1;
        }
    }

    last = start + len - 1;
    real_start = start & -host_page_size;
    real_last = ROUND_UP(last, host_page_size) - 1;

    /*
     * Handle the start and end of the mapping.
     */
    if (real_start < start) {
        abi_ulong real_page_last = real_start + host_page_size - 1;
        if (last <= real_page_last) {
            /* Entire allocation a subset of one host page. */
            if (!mmap_frag(real_start, start, last, target_prot,
                           flags, fd, offset)) {
                return -1;
            }
            return mmap_end(start, last, -1, 0, flags, page_flags);
        }

        if (!mmap_frag(real_start, start, real_page_last, target_prot,
                       flags, fd, offset)) {
            return -1;
        }
        real_start = real_page_last + 1;
    }

    if (last < real_last) {
        abi_ulong real_page_start = real_last - host_page_size + 1;
        if (!mmap_frag(real_page_start, real_page_start, last,
                       target_prot, flags, fd,
                       offset + real_page_start - start)) {
            return -1;
        }
        real_last = real_page_start - 1;
    }

    if (real_start > real_last) {
        return mmap_end(start, last, -1, 0, flags, page_flags);
    }

    /*
     * Handle the middle of the mapping.
     */

    host_len = real_last - real_start + 1;
    want_p += real_start - start;

    if (flags & MAP_ANONYMOUS) {
        p = mmap(want_p, host_len, host_prot, flags, -1, 0);
    } else if (!misaligned_offset) {
        p = mmap(want_p, host_len, host_prot, flags, fd,
                 offset + real_start - start);
    } else {
        p = mmap(want_p, host_len, host_prot | PROT_WRITE,
                 flags | MAP_ANONYMOUS, -1, 0);
    }
    if (p != want_p) {
        if (p != MAP_FAILED) {
            do_munmap(p, host_len);
            errno = EEXIST;
        }
        return -1;
    }

    if (misaligned_offset) {
        /* TODO: The read could be short. */
        if (pread(fd, p, host_len, offset + real_start - start) != host_len) {
            do_munmap(p, host_len);
            return -1;
        }
        if (!(host_prot & PROT_WRITE)) {
            mprotect(p, host_len, host_prot);
        }
    }

    return mmap_end(start, last, -1, 0, flags, page_flags);
}

static abi_long target_mmap__locked(abi_ulong start, abi_ulong len,
                                    int target_prot, int flags, int page_flags,
                                    int fd, off_t offset)
{
    int host_page_size = qemu_real_host_page_size();
    int host_prot;

    /*
     * For reserved_va, we are in full control of the allocation.
     * Find a suitable hole and convert to MAP_FIXED.
     */
    if (reserved_va) {
        if (flags & MAP_FIXED_NOREPLACE) {
            /* Validate that the chosen range is empty. */
            if (!page_check_range_empty(start, start + len - 1)) {
                errno = EEXIST;
                return -1;
            }
            flags = (flags & ~MAP_FIXED_NOREPLACE) | MAP_FIXED;
        } else if (!(flags & MAP_FIXED)) {
            abi_ulong real_start = start & -host_page_size;
            off_t host_offset = offset & -host_page_size;
            size_t real_len = len + offset - host_offset;
            abi_ulong align = MAX(host_page_size, TARGET_PAGE_SIZE);

            start = mmap_find_vma(real_start, real_len, align);
            if (start == (abi_ulong)-1) {
                errno = ENOMEM;
                return -1;
            }
            start += offset - host_offset;
            flags |= MAP_FIXED;
        }
    }

    host_prot = target_to_host_prot(target_prot);

    if (host_page_size == TARGET_PAGE_SIZE) {
        return mmap_h_eq_g(start, len, host_prot, flags,
                           page_flags, fd, offset);
    } else if (host_page_size < TARGET_PAGE_SIZE) {
        return mmap_h_lt_g(start, len, host_prot, flags,
                           page_flags, fd, offset, host_page_size);
    } else {
        return mmap_h_gt_g(start, len, target_prot, host_prot, flags,
                           page_flags, fd, offset, host_page_size);
    }
}

/* NOTE: all the constants are the HOST ones */
abi_long target_mmap(abi_ulong start, abi_ulong len, int target_prot,
                     int flags, int fd, off_t offset)
{
    abi_long ret;
    int page_flags;

    trace_target_mmap(start, len, target_prot, flags, fd, offset);

    if (!len) {
        errno = EINVAL;
        return -1;
    }

    page_flags = validate_prot_to_pageflags(target_prot);
    if (!page_flags) {
        errno = EINVAL;
        return -1;
    }

    /* Also check for overflows... */
    len = TARGET_PAGE_ALIGN(len);
    if (!len || len != (size_t)len) {
        errno = ENOMEM;
        return -1;
    }

    if (offset & ~TARGET_PAGE_MASK) {
        errno = EINVAL;
        return -1;
    }
    if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
        if (start & ~TARGET_PAGE_MASK) {
            errno = EINVAL;
            return -1;
        }
        if (!guest_range_valid_untagged(start, len)) {
            errno = ENOMEM;
            return -1;
        }
    }

    mmap_lock();

    ret = target_mmap__locked(start, len, target_prot, flags,
                              page_flags, fd, offset);

    mmap_unlock();

    /*
     * If we're mapping shared memory, ensure we generate code for parallel
     * execution and flush old translations.  This will work up to the level
     * supported by the host -- anything that requires EXCP_ATOMIC will not
     * be atomic with respect to an external process.
     */
    if (ret != -1 && (flags & MAP_TYPE) != MAP_PRIVATE) {
        CPUState *cpu = thread_cpu;
        if (!(cpu->tcg_cflags & CF_PARALLEL)) {
            cpu->tcg_cflags |= CF_PARALLEL;
            tb_flush(cpu);
        }
    }

    return ret;
}

static int mmap_reserve_or_unmap(abi_ulong start, abi_ulong len)
{
    int host_page_size = qemu_real_host_page_size();
    abi_ulong real_start;
    abi_ulong real_last;
    abi_ulong real_len;
    abi_ulong last;
    abi_ulong a;
    void *host_start;
    int prot;

    last = start + len - 1;
    real_start = start & -host_page_size;
    real_last = ROUND_UP(last, host_page_size) - 1;

    /*
     * If guest pages remain on the first or last host pages,
     * adjust the deallocation to retain those guest pages.
     * The single page special case is required for the last page,
     * lest real_start overflow to zero.
     */
    if (real_last - real_start < host_page_size) {
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
            real_start += host_page_size;
        }

        for (prot = 0, a = last; a < real_last; a += TARGET_PAGE_SIZE) {
            prot |= page_get_flags(a + 1);
        }
        if (prot != 0) {
            real_last -= host_page_size;
        }

        if (real_last < real_start) {
            return 0;
        }
    }

    real_len = real_last - real_start + 1;
    host_start = g2h_untagged(real_start);

    return do_munmap(host_start, real_len);
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

#if defined(__arm__) || defined(__mips__) || defined(__sparc__)
#define HOST_FORCE_SHMLBA 1
#else
#define HOST_FORCE_SHMLBA 0
#endif

abi_ulong target_shmat(CPUArchState *cpu_env, int shmid,
                       abi_ulong shmaddr, int shmflg)
{
    CPUState *cpu = env_cpu(cpu_env);
    struct shmid_ds shm_info;
    int ret;
    int h_pagesize;
    int t_shmlba, h_shmlba, m_shmlba;
    size_t t_len, h_len, m_len;

    /* shmat pointers are always untagged */

    /*
     * Because we can't use host shmat() unless the address is sufficiently
     * aligned for the host, we'll need to check both.
     * TODO: Could be fixed with softmmu.
     */
    t_shmlba = target_shmlba(cpu_env);
    h_pagesize = qemu_real_host_page_size();
    h_shmlba = (HOST_FORCE_SHMLBA ? SHMLBA : h_pagesize);
    m_shmlba = MAX(t_shmlba, h_shmlba);

    if (shmaddr) {
        if (shmaddr & (m_shmlba - 1)) {
            if (shmflg & SHM_RND) {
                /*
                 * The guest is allowing the kernel to round the address.
                 * Assume that the guest is ok with us rounding to the
                 * host required alignment too.  Anyway if we don't, we'll
                 * get an error from the kernel.
                 */
                shmaddr &= ~(m_shmlba - 1);
                if (shmaddr == 0 && (shmflg & SHM_REMAP)) {
                    return -TARGET_EINVAL;
                }
            } else {
                int require = TARGET_PAGE_SIZE;
#ifdef TARGET_FORCE_SHMLBA
                require = t_shmlba;
#endif
                /*
                 * Include host required alignment, as otherwise we cannot
                 * use host shmat at all.
                 */
                require = MAX(require, h_shmlba);
                if (shmaddr & (require - 1)) {
                    return -TARGET_EINVAL;
                }
            }
        }
    } else {
        if (shmflg & SHM_REMAP) {
            return -TARGET_EINVAL;
        }
    }
    /* All rounding now manually concluded. */
    shmflg &= ~SHM_RND;

    /* Find out the length of the shared memory segment. */
    ret = get_errno(shmctl(shmid, IPC_STAT, &shm_info));
    if (is_error(ret)) {
        /* can't get length, bail out */
        return ret;
    }
    t_len = TARGET_PAGE_ALIGN(shm_info.shm_segsz);
    h_len = ROUND_UP(shm_info.shm_segsz, h_pagesize);
    m_len = MAX(t_len, h_len);

    if (!guest_range_valid_untagged(shmaddr, m_len)) {
        return -TARGET_EINVAL;
    }

    WITH_MMAP_LOCK_GUARD() {
        bool mapped = false;
        void *want, *test;
        abi_ulong last;

        if (!shmaddr) {
            shmaddr = mmap_find_vma(0, m_len, m_shmlba);
            if (shmaddr == -1) {
                return -TARGET_ENOMEM;
            }
            mapped = !reserved_va;
        } else if (shmflg & SHM_REMAP) {
            /*
             * If host page size > target page size, the host shmat may map
             * more memory than the guest expects.  Reject a mapping that
             * would replace memory in the unexpected gap.
             * TODO: Could be fixed with softmmu.
             */
            if (t_len < h_len &&
                !page_check_range_empty(shmaddr + t_len,
                                        shmaddr + h_len - 1)) {
                return -TARGET_EINVAL;
            }
        } else {
            if (!page_check_range_empty(shmaddr, shmaddr + m_len - 1)) {
                return -TARGET_EINVAL;
            }
        }

        /* All placement is now complete. */
        want = (void *)g2h_untagged(shmaddr);

        /*
         * Map anonymous pages across the entire range, then remap with
         * the shared memory.  This is required for a number of corner
         * cases for which host and guest page sizes differ.
         */
        if (h_len != t_len) {
            int mmap_p = PROT_READ | (shmflg & SHM_RDONLY ? 0 : PROT_WRITE);
            int mmap_f = MAP_PRIVATE | MAP_ANONYMOUS
                       | (reserved_va || (shmflg & SHM_REMAP)
                          ? MAP_FIXED : MAP_FIXED_NOREPLACE);

            test = mmap(want, m_len, mmap_p, mmap_f, -1, 0);
            if (unlikely(test != want)) {
                /* shmat returns EINVAL not EEXIST like mmap. */
                ret = (test == MAP_FAILED && errno != EEXIST
                       ? get_errno(-1) : -TARGET_EINVAL);
                if (mapped) {
                    do_munmap(want, m_len);
                }
                return ret;
            }
            mapped = true;
        }

        if (reserved_va || mapped) {
            shmflg |= SHM_REMAP;
        }
        test = shmat(shmid, want, shmflg);
        if (test == MAP_FAILED) {
            ret = get_errno(-1);
            if (mapped) {
                do_munmap(want, m_len);
            }
            return ret;
        }
        assert(test == want);

        last = shmaddr + m_len - 1;
        page_set_flags(shmaddr, last,
                       PAGE_VALID | PAGE_RESET | PAGE_READ |
                       (shmflg & SHM_RDONLY ? 0 : PAGE_WRITE) |
                       (shmflg & SHM_EXEC ? PAGE_EXEC : 0));

        shm_region_rm_complete(shmaddr, last);
        shm_region_add(shmaddr, last);
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

    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        FILE *f = qemu_log_trylock();
        if (f) {
            fprintf(f, "page layout changed following shmat\n");
            page_dump(f);
            qemu_log_unlock(f);
        }
    }
    return shmaddr;
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
