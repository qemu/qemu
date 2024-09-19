/*
 * Linux UFFD-WP support
 *
 * Copyright Virtuozzo GmbH, 2020
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/userfaultfd.h"
#include "trace.h"
#include <poll.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

typedef enum {
    UFFD_UNINITIALIZED = 0,
    UFFD_USE_DEV_PATH,
    UFFD_USE_SYSCALL,
} uffd_open_mode;

int uffd_open(int flags)
{
#if defined(__NR_userfaultfd)
    static uffd_open_mode open_mode;
    static int uffd_dev;

    /* Detect how to generate uffd desc when run the 1st time */
    if (open_mode == UFFD_UNINITIALIZED) {
        /*
         * Make /dev/userfaultfd the default approach because it has better
         * permission controls, meanwhile allows kernel faults without any
         * privilege requirement (e.g. SYS_CAP_PTRACE).
         */
        uffd_dev = open("/dev/userfaultfd", O_RDWR | O_CLOEXEC);
        if (uffd_dev >= 0) {
            open_mode = UFFD_USE_DEV_PATH;
        } else {
            /* Fallback to the system call */
            open_mode = UFFD_USE_SYSCALL;
        }
        trace_uffd_detect_open_mode(open_mode);
    }

    if (open_mode == UFFD_USE_DEV_PATH) {
        assert(uffd_dev >= 0);
        return ioctl(uffd_dev, USERFAULTFD_IOC_NEW, flags);
    }

    return syscall(__NR_userfaultfd, flags);
#else
    return -EINVAL;
#endif
}

/**
 * uffd_query_features: query UFFD features
 *
 * Returns: 0 on success, negative value in case of an error
 *
 * @features: parameter to receive 'uffdio_api.features'
 */
int uffd_query_features(uint64_t *features)
{
    int uffd_fd;
    struct uffdio_api api_struct = { 0 };
    int ret = -1;

    uffd_fd = uffd_open(O_CLOEXEC);
    if (uffd_fd < 0) {
        trace_uffd_query_features_nosys(errno);
        return -1;
    }

    api_struct.api = UFFD_API;
    api_struct.features = 0;

    if (ioctl(uffd_fd, UFFDIO_API, &api_struct)) {
        trace_uffd_query_features_api_failed(errno);
        goto out;
    }
    *features = api_struct.features;
    ret = 0;

out:
    close(uffd_fd);
    return ret;
}

/**
 * uffd_create_fd: create UFFD file descriptor
 *
 * Returns non-negative file descriptor or negative value in case of an error
 *
 * @features: UFFD features to request
 * @non_blocking: create UFFD file descriptor for non-blocking operation
 */
int uffd_create_fd(uint64_t features, bool non_blocking)
{
    int uffd_fd;
    int flags;
    struct uffdio_api api_struct = { 0 };
    uint64_t ioctl_mask = BIT(_UFFDIO_REGISTER) | BIT(_UFFDIO_UNREGISTER);

    flags = O_CLOEXEC | (non_blocking ? O_NONBLOCK : 0);
    uffd_fd = uffd_open(flags);
    if (uffd_fd < 0) {
        trace_uffd_create_fd_nosys(errno);
        return -1;
    }

    api_struct.api = UFFD_API;
    api_struct.features = features;
    if (ioctl(uffd_fd, UFFDIO_API, &api_struct)) {
        trace_uffd_create_fd_api_failed(errno);
        goto fail;
    }
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        trace_uffd_create_fd_api_noioctl(ioctl_mask, api_struct.ioctls);
        goto fail;
    }

    return uffd_fd;

fail:
    close(uffd_fd);
    return -1;
}

/**
 * uffd_close_fd: close UFFD file descriptor
 *
 * @uffd_fd: UFFD file descriptor
 */
void uffd_close_fd(int uffd_fd)
{
    assert(uffd_fd >= 0);
    close(uffd_fd);
}

/**
 * uffd_register_memory: register memory range via UFFD-IO
 *
 * Returns 0 in case of success, negative value in case of an error
 *
 * @uffd_fd: UFFD file descriptor
 * @addr: base address of memory range
 * @length: length of memory range
 * @mode: UFFD register mode (UFFDIO_REGISTER_MODE_MISSING, ...)
 * @ioctls: optional pointer to receive supported IOCTL mask
 */
int uffd_register_memory(int uffd_fd, void *addr, uint64_t length,
        uint64_t mode, uint64_t *ioctls)
{
    struct uffdio_register uffd_register;

    uffd_register.range.start = (uintptr_t) addr;
    uffd_register.range.len = length;
    uffd_register.mode = mode;

    if (ioctl(uffd_fd, UFFDIO_REGISTER, &uffd_register)) {
        trace_uffd_register_memory_failed(addr, length, mode, errno);
        return -1;
    }
    if (ioctls) {
        *ioctls = uffd_register.ioctls;
    }

    return 0;
}

/**
 * uffd_unregister_memory: un-register memory range with UFFD-IO
 *
 * Returns 0 in case of success, negative value in case of an error
 *
 * @uffd_fd: UFFD file descriptor
 * @addr: base address of memory range
 * @length: length of memory range
 */
int uffd_unregister_memory(int uffd_fd, void *addr, uint64_t length)
{
    struct uffdio_range uffd_range;

    uffd_range.start = (uintptr_t) addr;
    uffd_range.len = length;

    if (ioctl(uffd_fd, UFFDIO_UNREGISTER, &uffd_range)) {
        trace_uffd_unregister_memory_failed(addr, length, errno);
        return -1;
    }

    return 0;
}

/**
 * uffd_change_protection: protect/un-protect memory range for writes via UFFD-IO
 *
 * Returns 0 on success, negative value in case of error
 *
 * @uffd_fd: UFFD file descriptor
 * @addr: base address of memory range
 * @length: length of memory range
 * @wp: write-protect/unprotect
 * @dont_wake: do not wake threads waiting on wr-protected page
 */
int uffd_change_protection(int uffd_fd, void *addr, uint64_t length,
        bool wp, bool dont_wake)
{
    struct uffdio_writeprotect uffd_writeprotect;

    uffd_writeprotect.range.start = (uintptr_t) addr;
    uffd_writeprotect.range.len = length;
    if (!wp && dont_wake) {
        /* DONTWAKE is meaningful only on protection release */
        uffd_writeprotect.mode = UFFDIO_WRITEPROTECT_MODE_DONTWAKE;
    } else {
        uffd_writeprotect.mode = (wp ? UFFDIO_WRITEPROTECT_MODE_WP : 0);
    }

    if (ioctl(uffd_fd, UFFDIO_WRITEPROTECT, &uffd_writeprotect)) {
        error_report("uffd_change_protection() failed: addr=%p len=%" PRIu64
                " mode=%" PRIx64 " errno=%i", addr, length,
                (uint64_t) uffd_writeprotect.mode, errno);
        return -1;
    }

    return 0;
}

/**
 * uffd_copy_page: copy range of pages to destination via UFFD-IO
 *
 * Copy range of source pages to the destination to resolve
 * missing page fault somewhere in the destination range.
 *
 * Returns 0 on success, -errno in case of an error
 *
 * @uffd_fd: UFFD file descriptor
 * @dst_addr: destination base address
 * @src_addr: source base address
 * @length: length of the range to copy
 * @dont_wake: do not wake threads waiting on missing page
 */
int uffd_copy_page(int uffd_fd, void *dst_addr, void *src_addr,
        uint64_t length, bool dont_wake)
{
    struct uffdio_copy uffd_copy;

    uffd_copy.dst = (uintptr_t) dst_addr;
    uffd_copy.src = (uintptr_t) src_addr;
    uffd_copy.len = length;
    uffd_copy.mode = dont_wake ? UFFDIO_COPY_MODE_DONTWAKE : 0;

    if (ioctl(uffd_fd, UFFDIO_COPY, &uffd_copy)) {
        int e = errno;
        error_report("uffd_copy_page() failed: dst_addr=%p src_addr=%p length=%" PRIu64
                " mode=%" PRIx64 " errno=%i", dst_addr, src_addr,
                length, (uint64_t) uffd_copy.mode, e);
        return -e;
    }

    return 0;
}

/**
 * uffd_zero_page: fill range of pages with zeroes via UFFD-IO
 *
 * Fill range pages with zeroes to resolve missing page fault within the range.
 *
 * Returns 0 on success, -errno in case of an error
 *
 * @uffd_fd: UFFD file descriptor
 * @addr: base address
 * @length: length of the range to fill with zeroes
 * @dont_wake: do not wake threads waiting on missing page
 */
int uffd_zero_page(int uffd_fd, void *addr, uint64_t length, bool dont_wake)
{
    struct uffdio_zeropage uffd_zeropage;

    uffd_zeropage.range.start = (uintptr_t) addr;
    uffd_zeropage.range.len = length;
    uffd_zeropage.mode = dont_wake ? UFFDIO_ZEROPAGE_MODE_DONTWAKE : 0;

    if (ioctl(uffd_fd, UFFDIO_ZEROPAGE, &uffd_zeropage)) {
        int e = errno;
        error_report("uffd_zero_page() failed: addr=%p length=%" PRIu64
                " mode=%" PRIx64 " errno=%i", addr, length,
                (uint64_t) uffd_zeropage.mode, e);
        return -e;
    }

    return 0;
}

/**
 * uffd_wakeup: wake up threads waiting on page UFFD-managed page fault resolution
 *
 * Wake up threads waiting on any page/pages from the designated range.
 * The main use case is when during some period, page faults are resolved
 * via UFFD-IO IOCTLs with MODE_DONTWAKE flag set, then after that all waits
 * for the whole memory range are satisfied in a single call to uffd_wakeup().
 *
 * Returns 0 on success, -errno in case of an error
 *
 * @uffd_fd: UFFD file descriptor
 * @addr: base address
 * @length: length of the range
 */
int uffd_wakeup(int uffd_fd, void *addr, uint64_t length)
{
    struct uffdio_range uffd_range;

    uffd_range.start = (uintptr_t) addr;
    uffd_range.len = length;

    if (ioctl(uffd_fd, UFFDIO_WAKE, &uffd_range)) {
        int e = errno;
        error_report("uffd_wakeup() failed: addr=%p length=%" PRIu64 " errno=%i",
                addr, length, e);
        return -e;
    }

    return 0;
}

/**
 * uffd_read_events: read pending UFFD events
 *
 * Returns number of fetched messages, 0 if non is available or
 * negative value in case of an error
 *
 * @uffd_fd: UFFD file descriptor
 * @msgs: pointer to message buffer
 * @count: number of messages that can fit in the buffer
 */
int uffd_read_events(int uffd_fd, struct uffd_msg *msgs, int count)
{
    ssize_t res;
    do {
        res = read(uffd_fd, msgs, count * sizeof(struct uffd_msg));
    } while (res < 0 && errno == EINTR);

    if ((res < 0 && errno == EAGAIN)) {
        return 0;
    }
    if (res < 0) {
        error_report("uffd_read_events() failed: errno=%i", errno);
        return -1;
    }

    return (int) (res / sizeof(struct uffd_msg));
}
