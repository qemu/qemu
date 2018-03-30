/*
 * Postcopy migration for RAM
 *
 * Copyright 2013-2015 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Dave Gilbert  <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * Postcopy is a migration technique where the execution flips from the
 * source to the destination before all the data has been copied.
 */

#include "qemu/osdep.h"
#include "exec/target_page.h"
#include "migration.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "ram.h"
#include "qapi/error.h"
#include "qemu/notify.h"
#include "sysemu/sysemu.h"
#include "sysemu/balloon.h"
#include "qemu/error-report.h"
#include "trace.h"

/* Arbitrary limit on size of each discard command,
 * keeps them around ~200 bytes
 */
#define MAX_DISCARDS_PER_COMMAND 12

struct PostcopyDiscardState {
    const char *ramblock_name;
    uint16_t cur_entry;
    /*
     * Start and length of a discard range (bytes)
     */
    uint64_t start_list[MAX_DISCARDS_PER_COMMAND];
    uint64_t length_list[MAX_DISCARDS_PER_COMMAND];
    unsigned int nsentwords;
    unsigned int nsentcmds;
};

static NotifierWithReturnList postcopy_notifier_list;

void postcopy_infrastructure_init(void)
{
    notifier_with_return_list_init(&postcopy_notifier_list);
}

void postcopy_add_notifier(NotifierWithReturn *nn)
{
    notifier_with_return_list_add(&postcopy_notifier_list, nn);
}

void postcopy_remove_notifier(NotifierWithReturn *n)
{
    notifier_with_return_remove(n);
}

int postcopy_notify(enum PostcopyNotifyReason reason, Error **errp)
{
    struct PostcopyNotifyData pnd;
    pnd.reason = reason;
    pnd.errp = errp;

    return notifier_with_return_list_notify(&postcopy_notifier_list,
                                            &pnd);
}

/* Postcopy needs to detect accesses to pages that haven't yet been copied
 * across, and efficiently map new pages in, the techniques for doing this
 * are target OS specific.
 */
#if defined(__linux__)

#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <asm/types.h> /* for __u64 */
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>


/**
 * receive_ufd_features: check userfault fd features, to request only supported
 * features in the future.
 *
 * Returns: true on success
 *
 * __NR_userfaultfd - should be checked before
 *  @features: out parameter will contain uffdio_api.features provided by kernel
 *              in case of success
 */
static bool receive_ufd_features(uint64_t *features)
{
    struct uffdio_api api_struct = {0};
    int ufd;
    bool ret = true;

    /* if we are here __NR_userfaultfd should exists */
    ufd = syscall(__NR_userfaultfd, O_CLOEXEC);
    if (ufd == -1) {
        error_report("%s: syscall __NR_userfaultfd failed: %s", __func__,
                     strerror(errno));
        return false;
    }

    /* ask features */
    api_struct.api = UFFD_API;
    api_struct.features = 0;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        error_report("%s: UFFDIO_API failed: %s", __func__,
                     strerror(errno));
        ret = false;
        goto release_ufd;
    }

    *features = api_struct.features;

release_ufd:
    close(ufd);
    return ret;
}

/**
 * request_ufd_features: this function should be called only once on a newly
 * opened ufd, subsequent calls will lead to error.
 *
 * Returns: true on succes
 *
 * @ufd: fd obtained from userfaultfd syscall
 * @features: bit mask see UFFD_API_FEATURES
 */
static bool request_ufd_features(int ufd, uint64_t features)
{
    struct uffdio_api api_struct = {0};
    uint64_t ioctl_mask;

    api_struct.api = UFFD_API;
    api_struct.features = features;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        error_report("%s failed: UFFDIO_API failed: %s", __func__,
                     strerror(errno));
        return false;
    }

    ioctl_mask = (__u64)1 << _UFFDIO_REGISTER |
                 (__u64)1 << _UFFDIO_UNREGISTER;
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        error_report("Missing userfault features: %" PRIx64,
                     (uint64_t)(~api_struct.ioctls & ioctl_mask));
        return false;
    }

    return true;
}

static bool ufd_check_and_apply(int ufd, MigrationIncomingState *mis)
{
    uint64_t asked_features = 0;
    static uint64_t supported_features;

    /*
     * it's not possible to
     * request UFFD_API twice per one fd
     * userfault fd features is persistent
     */
    if (!supported_features) {
        if (!receive_ufd_features(&supported_features)) {
            error_report("%s failed", __func__);
            return false;
        }
    }

    /*
     * request features, even if asked_features is 0, due to
     * kernel expects UFFD_API before UFFDIO_REGISTER, per
     * userfault file descriptor
     */
    if (!request_ufd_features(ufd, asked_features)) {
        error_report("%s failed: features %" PRIu64, __func__,
                     asked_features);
        return false;
    }

    if (getpagesize() != ram_pagesize_summary()) {
        bool have_hp = false;
        /* We've got a huge page */
#ifdef UFFD_FEATURE_MISSING_HUGETLBFS
        have_hp = supported_features & UFFD_FEATURE_MISSING_HUGETLBFS;
#endif
        if (!have_hp) {
            error_report("Userfault on this host does not support huge pages");
            return false;
        }
    }
    return true;
}

/* Callback from postcopy_ram_supported_by_host block iterator.
 */
static int test_ramblock_postcopiable(const char *block_name, void *host_addr,
                             ram_addr_t offset, ram_addr_t length, void *opaque)
{
    RAMBlock *rb = qemu_ram_block_by_name(block_name);
    size_t pagesize = qemu_ram_pagesize(rb);

    if (length % pagesize) {
        error_report("Postcopy requires RAM blocks to be a page size multiple,"
                     " block %s is 0x" RAM_ADDR_FMT " bytes with a "
                     "page size of 0x%zx", block_name, length, pagesize);
        return 1;
    }
    return 0;
}

/*
 * Note: This has the side effect of munlock'ing all of RAM, that's
 * normally fine since if the postcopy succeeds it gets turned back on at the
 * end.
 */
bool postcopy_ram_supported_by_host(MigrationIncomingState *mis)
{
    long pagesize = getpagesize();
    int ufd = -1;
    bool ret = false; /* Error unless we change it */
    void *testarea = NULL;
    struct uffdio_register reg_struct;
    struct uffdio_range range_struct;
    uint64_t feature_mask;
    Error *local_err = NULL;

    if (qemu_target_page_size() > pagesize) {
        error_report("Target page size bigger than host page size");
        goto out;
    }

    ufd = syscall(__NR_userfaultfd, O_CLOEXEC);
    if (ufd == -1) {
        error_report("%s: userfaultfd not available: %s", __func__,
                     strerror(errno));
        goto out;
    }

    /* Give devices a chance to object */
    if (postcopy_notify(POSTCOPY_NOTIFY_PROBE, &local_err)) {
        error_report_err(local_err);
        goto out;
    }

    /* Version and features check */
    if (!ufd_check_and_apply(ufd, mis)) {
        goto out;
    }

    /* We don't support postcopy with shared RAM yet */
    if (qemu_ram_foreach_block(test_ramblock_postcopiable, NULL)) {
        goto out;
    }

    /*
     * userfault and mlock don't go together; we'll put it back later if
     * it was enabled.
     */
    if (munlockall()) {
        error_report("%s: munlockall: %s", __func__,  strerror(errno));
        return -1;
    }

    /*
     *  We need to check that the ops we need are supported on anon memory
     *  To do that we need to register a chunk and see the flags that
     *  are returned.
     */
    testarea = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE |
                                    MAP_ANONYMOUS, -1, 0);
    if (testarea == MAP_FAILED) {
        error_report("%s: Failed to map test area: %s", __func__,
                     strerror(errno));
        goto out;
    }
    g_assert(((size_t)testarea & (pagesize-1)) == 0);

    reg_struct.range.start = (uintptr_t)testarea;
    reg_struct.range.len = pagesize;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    if (ioctl(ufd, UFFDIO_REGISTER, &reg_struct)) {
        error_report("%s userfault register: %s", __func__, strerror(errno));
        goto out;
    }

    range_struct.start = (uintptr_t)testarea;
    range_struct.len = pagesize;
    if (ioctl(ufd, UFFDIO_UNREGISTER, &range_struct)) {
        error_report("%s userfault unregister: %s", __func__, strerror(errno));
        goto out;
    }

    feature_mask = (__u64)1 << _UFFDIO_WAKE |
                   (__u64)1 << _UFFDIO_COPY |
                   (__u64)1 << _UFFDIO_ZEROPAGE;
    if ((reg_struct.ioctls & feature_mask) != feature_mask) {
        error_report("Missing userfault map features: %" PRIx64,
                     (uint64_t)(~reg_struct.ioctls & feature_mask));
        goto out;
    }

    /* Success! */
    ret = true;
out:
    if (testarea) {
        munmap(testarea, pagesize);
    }
    if (ufd != -1) {
        close(ufd);
    }
    return ret;
}

/*
 * Setup an area of RAM so that it *can* be used for postcopy later; this
 * must be done right at the start prior to pre-copy.
 * opaque should be the MIS.
 */
static int init_range(const char *block_name, void *host_addr,
                      ram_addr_t offset, ram_addr_t length, void *opaque)
{
    trace_postcopy_init_range(block_name, host_addr, offset, length);

    /*
     * We need the whole of RAM to be truly empty for postcopy, so things
     * like ROMs and any data tables built during init must be zero'd
     * - we're going to get the copy from the source anyway.
     * (Precopy will just overwrite this data, so doesn't need the discard)
     */
    if (ram_discard_range(block_name, 0, length)) {
        return -1;
    }

    return 0;
}

/*
 * At the end of migration, undo the effects of init_range
 * opaque should be the MIS.
 */
static int cleanup_range(const char *block_name, void *host_addr,
                        ram_addr_t offset, ram_addr_t length, void *opaque)
{
    MigrationIncomingState *mis = opaque;
    struct uffdio_range range_struct;
    trace_postcopy_cleanup_range(block_name, host_addr, offset, length);

    /*
     * We turned off hugepage for the precopy stage with postcopy enabled
     * we can turn it back on now.
     */
    qemu_madvise(host_addr, length, QEMU_MADV_HUGEPAGE);

    /*
     * We can also turn off userfault now since we should have all the
     * pages.   It can be useful to leave it on to debug postcopy
     * if you're not sure it's always getting every page.
     */
    range_struct.start = (uintptr_t)host_addr;
    range_struct.len = length;

    if (ioctl(mis->userfault_fd, UFFDIO_UNREGISTER, &range_struct)) {
        error_report("%s: userfault unregister %s", __func__, strerror(errno));

        return -1;
    }

    return 0;
}

/*
 * Initialise postcopy-ram, setting the RAM to a state where we can go into
 * postcopy later; must be called prior to any precopy.
 * called from arch_init's similarly named ram_postcopy_incoming_init
 */
int postcopy_ram_incoming_init(MigrationIncomingState *mis, size_t ram_pages)
{
    if (qemu_ram_foreach_block(init_range, NULL)) {
        return -1;
    }

    return 0;
}

/*
 * At the end of a migration where postcopy_ram_incoming_init was called.
 */
int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    trace_postcopy_ram_incoming_cleanup_entry();

    if (mis->have_fault_thread) {
        Error *local_err = NULL;

        if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_END, &local_err)) {
            error_report_err(local_err);
            return -1;
        }

        if (qemu_ram_foreach_block(cleanup_range, mis)) {
            return -1;
        }
        /* Let the fault thread quit */
        atomic_set(&mis->fault_thread_quit, 1);
        postcopy_fault_thread_notify(mis);
        trace_postcopy_ram_incoming_cleanup_join();
        qemu_thread_join(&mis->fault_thread);

        trace_postcopy_ram_incoming_cleanup_closeuf();
        close(mis->userfault_fd);
        close(mis->userfault_event_fd);
        mis->have_fault_thread = false;
    }

    qemu_balloon_inhibit(false);

    if (enable_mlock) {
        if (os_mlock() < 0) {
            error_report("mlock: %s", strerror(errno));
            /*
             * It doesn't feel right to fail at this point, we have a valid
             * VM state.
             */
        }
    }

    postcopy_state_set(POSTCOPY_INCOMING_END);

    if (mis->postcopy_tmp_page) {
        munmap(mis->postcopy_tmp_page, mis->largest_page_size);
        mis->postcopy_tmp_page = NULL;
    }
    if (mis->postcopy_tmp_zero_page) {
        munmap(mis->postcopy_tmp_zero_page, mis->largest_page_size);
        mis->postcopy_tmp_zero_page = NULL;
    }
    trace_postcopy_ram_incoming_cleanup_exit();
    return 0;
}

/*
 * Disable huge pages on an area
 */
static int nhp_range(const char *block_name, void *host_addr,
                    ram_addr_t offset, ram_addr_t length, void *opaque)
{
    trace_postcopy_nhp_range(block_name, host_addr, offset, length);

    /*
     * Before we do discards we need to ensure those discards really
     * do delete areas of the page, even if THP thinks a hugepage would
     * be a good idea, so force hugepages off.
     */
    qemu_madvise(host_addr, length, QEMU_MADV_NOHUGEPAGE);

    return 0;
}

/*
 * Userfault requires us to mark RAM as NOHUGEPAGE prior to discard
 * however leaving it until after precopy means that most of the precopy
 * data is still THPd
 */
int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    if (qemu_ram_foreach_block(nhp_range, mis)) {
        return -1;
    }

    postcopy_state_set(POSTCOPY_INCOMING_DISCARD);

    return 0;
}

/*
 * Mark the given area of RAM as requiring notification to unwritten areas
 * Used as a  callback on qemu_ram_foreach_block.
 *   host_addr: Base of area to mark
 *   offset: Offset in the whole ram arena
 *   length: Length of the section
 *   opaque: MigrationIncomingState pointer
 * Returns 0 on success
 */
static int ram_block_enable_notify(const char *block_name, void *host_addr,
                                   ram_addr_t offset, ram_addr_t length,
                                   void *opaque)
{
    MigrationIncomingState *mis = opaque;
    struct uffdio_register reg_struct;

    reg_struct.range.start = (uintptr_t)host_addr;
    reg_struct.range.len = length;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    /* Now tell our userfault_fd that it's responsible for this area */
    if (ioctl(mis->userfault_fd, UFFDIO_REGISTER, &reg_struct)) {
        error_report("%s userfault register: %s", __func__, strerror(errno));
        return -1;
    }
    if (!(reg_struct.ioctls & ((__u64)1 << _UFFDIO_COPY))) {
        error_report("%s userfault: Region doesn't support COPY", __func__);
        return -1;
    }
    if (reg_struct.ioctls & ((__u64)1 << _UFFDIO_ZEROPAGE)) {
        RAMBlock *rb = qemu_ram_block_by_name(block_name);
        qemu_ram_set_uf_zeroable(rb);
    }

    return 0;
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    struct uffdio_range range;
    int ret;
    trace_postcopy_wake_shared(client_addr, qemu_ram_get_idstr(rb));
    range.start = client_addr & ~(pagesize - 1);
    range.len = pagesize;
    ret = ioctl(pcfd->fd, UFFDIO_WAKE, &range);
    if (ret) {
        error_report("%s: Failed to wake: %zx in %s (%s)",
                     __func__, (size_t)client_addr, qemu_ram_get_idstr(rb),
                     strerror(errno));
    }
    return ret;
}

/*
 * Callback from shared fault handlers to ask for a page,
 * the page must be specified by a RAMBlock and an offset in that rb
 * Note: Only for use by shared fault handlers (in fault thread)
 */
int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    uint64_t aligned_rbo = rb_offset & ~(pagesize - 1);
    MigrationIncomingState *mis = migration_incoming_get_current();

    trace_postcopy_request_shared_page(pcfd->idstr, qemu_ram_get_idstr(rb),
                                       rb_offset);
    if (ramblock_recv_bitmap_test_byte_offset(rb, aligned_rbo)) {
        trace_postcopy_request_shared_page_present(pcfd->idstr,
                                        qemu_ram_get_idstr(rb), rb_offset);
        return postcopy_wake_shared(pcfd, client_addr, rb);
    }
    if (rb != mis->last_rb) {
        mis->last_rb = rb;
        migrate_send_rp_req_pages(mis, qemu_ram_get_idstr(rb),
                                  aligned_rbo, pagesize);
    } else {
        /* Save some space */
        migrate_send_rp_req_pages(mis, NULL, aligned_rbo, pagesize);
    }
    return 0;
}

/*
 * Handle faults detected by the USERFAULT markings
 */
static void *postcopy_ram_fault_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    struct uffd_msg msg;
    int ret;
    size_t index;
    RAMBlock *rb = NULL;

    trace_postcopy_ram_fault_thread_entry();
    mis->last_rb = NULL; /* last RAMBlock we sent part of */
    qemu_sem_post(&mis->fault_thread_sem);

    struct pollfd *pfd;
    size_t pfd_len = 2 + mis->postcopy_remote_fds->len;

    pfd = g_new0(struct pollfd, pfd_len);

    pfd[0].fd = mis->userfault_fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = mis->userfault_event_fd;
    pfd[1].events = POLLIN; /* Waiting for eventfd to go positive */
    trace_postcopy_ram_fault_thread_fds_core(pfd[0].fd, pfd[1].fd);
    for (index = 0; index < mis->postcopy_remote_fds->len; index++) {
        struct PostCopyFD *pcfd = &g_array_index(mis->postcopy_remote_fds,
                                                 struct PostCopyFD, index);
        pfd[2 + index].fd = pcfd->fd;
        pfd[2 + index].events = POLLIN;
        trace_postcopy_ram_fault_thread_fds_extra(2 + index, pcfd->idstr,
                                                  pcfd->fd);
    }

    while (true) {
        ram_addr_t rb_offset;
        int poll_result;

        /*
         * We're mainly waiting for the kernel to give us a faulting HVA,
         * however we can be told to quit via userfault_quit_fd which is
         * an eventfd
         */

        poll_result = poll(pfd, pfd_len, -1 /* Wait forever */);
        if (poll_result == -1) {
            error_report("%s: userfault poll: %s", __func__, strerror(errno));
            break;
        }

        if (pfd[1].revents) {
            uint64_t tmp64 = 0;

            /* Consume the signal */
            if (read(mis->userfault_event_fd, &tmp64, 8) != 8) {
                /* Nothing obviously nicer than posting this error. */
                error_report("%s: read() failed", __func__);
            }

            if (atomic_read(&mis->fault_thread_quit)) {
                trace_postcopy_ram_fault_thread_quit();
                break;
            }
        }

        if (pfd[0].revents) {
            poll_result--;
            ret = read(mis->userfault_fd, &msg, sizeof(msg));
            if (ret != sizeof(msg)) {
                if (errno == EAGAIN) {
                    /*
                     * if a wake up happens on the other thread just after
                     * the poll, there is nothing to read.
                     */
                    continue;
                }
                if (ret < 0) {
                    error_report("%s: Failed to read full userfault "
                                 "message: %s",
                                 __func__, strerror(errno));
                    break;
                } else {
                    error_report("%s: Read %d bytes from userfaultfd "
                                 "expected %zd",
                                 __func__, ret, sizeof(msg));
                    break; /* Lost alignment, don't know what we'd read next */
                }
            }
            if (msg.event != UFFD_EVENT_PAGEFAULT) {
                error_report("%s: Read unexpected event %ud from userfaultfd",
                             __func__, msg.event);
                continue; /* It's not a page fault, shouldn't happen */
            }

            rb = qemu_ram_block_from_host(
                     (void *)(uintptr_t)msg.arg.pagefault.address,
                     true, &rb_offset);
            if (!rb) {
                error_report("postcopy_ram_fault_thread: Fault outside guest: %"
                             PRIx64, (uint64_t)msg.arg.pagefault.address);
                break;
            }

            rb_offset &= ~(qemu_ram_pagesize(rb) - 1);
            trace_postcopy_ram_fault_thread_request(msg.arg.pagefault.address,
                                                qemu_ram_get_idstr(rb),
                                                rb_offset);
            /*
             * Send the request to the source - we want to request one
             * of our host page sizes (which is >= TPS)
             */
            if (rb != mis->last_rb) {
                mis->last_rb = rb;
                migrate_send_rp_req_pages(mis, qemu_ram_get_idstr(rb),
                                         rb_offset, qemu_ram_pagesize(rb));
            } else {
                /* Save some space */
                migrate_send_rp_req_pages(mis, NULL,
                                         rb_offset, qemu_ram_pagesize(rb));
            }
        }

        /* Now handle any requests from external processes on shared memory */
        /* TODO: May need to handle devices deregistering during postcopy */
        for (index = 2; index < pfd_len && poll_result; index++) {
            if (pfd[index].revents) {
                struct PostCopyFD *pcfd =
                    &g_array_index(mis->postcopy_remote_fds,
                                   struct PostCopyFD, index - 2);

                poll_result--;
                if (pfd[index].revents & POLLERR) {
                    error_report("%s: POLLERR on poll %zd fd=%d",
                                 __func__, index, pcfd->fd);
                    pfd[index].events = 0;
                    continue;
                }

                ret = read(pcfd->fd, &msg, sizeof(msg));
                if (ret != sizeof(msg)) {
                    if (errno == EAGAIN) {
                        /*
                         * if a wake up happens on the other thread just after
                         * the poll, there is nothing to read.
                         */
                        continue;
                    }
                    if (ret < 0) {
                        error_report("%s: Failed to read full userfault "
                                     "message: %s (shared) revents=%d",
                                     __func__, strerror(errno),
                                     pfd[index].revents);
                        /*TODO: Could just disable this sharer */
                        break;
                    } else {
                        error_report("%s: Read %d bytes from userfaultfd "
                                     "expected %zd (shared)",
                                     __func__, ret, sizeof(msg));
                        /*TODO: Could just disable this sharer */
                        break; /*Lost alignment,don't know what we'd read next*/
                    }
                }
                if (msg.event != UFFD_EVENT_PAGEFAULT) {
                    error_report("%s: Read unexpected event %ud "
                                 "from userfaultfd (shared)",
                                 __func__, msg.event);
                    continue; /* It's not a page fault, shouldn't happen */
                }
                /* Call the device handler registered with us */
                ret = pcfd->handler(pcfd, &msg);
                if (ret) {
                    error_report("%s: Failed to resolve shared fault on %zd/%s",
                                 __func__, index, pcfd->idstr);
                    /* TODO: Fail? Disable this sharer? */
                }
            }
        }
    }
    trace_postcopy_ram_fault_thread_exit();
    g_free(pfd);
    return NULL;
}

int postcopy_ram_enable_notify(MigrationIncomingState *mis)
{
    /* Open the fd for the kernel to give us userfaults */
    mis->userfault_fd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (mis->userfault_fd == -1) {
        error_report("%s: Failed to open userfault fd: %s", __func__,
                     strerror(errno));
        return -1;
    }

    /*
     * Although the host check already tested the API, we need to
     * do the check again as an ABI handshake on the new fd.
     */
    if (!ufd_check_and_apply(mis->userfault_fd, mis)) {
        return -1;
    }

    /* Now an eventfd we use to tell the fault-thread to quit */
    mis->userfault_event_fd = eventfd(0, EFD_CLOEXEC);
    if (mis->userfault_event_fd == -1) {
        error_report("%s: Opening userfault_event_fd: %s", __func__,
                     strerror(errno));
        close(mis->userfault_fd);
        return -1;
    }

    qemu_sem_init(&mis->fault_thread_sem, 0);
    qemu_thread_create(&mis->fault_thread, "postcopy/fault",
                       postcopy_ram_fault_thread, mis, QEMU_THREAD_JOINABLE);
    qemu_sem_wait(&mis->fault_thread_sem);
    qemu_sem_destroy(&mis->fault_thread_sem);
    mis->have_fault_thread = true;

    /* Mark so that we get notified of accesses to unwritten areas */
    if (qemu_ram_foreach_block(ram_block_enable_notify, mis)) {
        return -1;
    }

    /*
     * Ballooning can mark pages as absent while we're postcopying
     * that would cause false userfaults.
     */
    qemu_balloon_inhibit(true);

    trace_postcopy_ram_enable_notify();

    return 0;
}

static int qemu_ufd_copy_ioctl(int userfault_fd, void *host_addr,
                               void *from_addr, uint64_t pagesize, RAMBlock *rb)
{
    int ret;
    if (from_addr) {
        struct uffdio_copy copy_struct;
        copy_struct.dst = (uint64_t)(uintptr_t)host_addr;
        copy_struct.src = (uint64_t)(uintptr_t)from_addr;
        copy_struct.len = pagesize;
        copy_struct.mode = 0;
        ret = ioctl(userfault_fd, UFFDIO_COPY, &copy_struct);
    } else {
        struct uffdio_zeropage zero_struct;
        zero_struct.range.start = (uint64_t)(uintptr_t)host_addr;
        zero_struct.range.len = pagesize;
        zero_struct.mode = 0;
        ret = ioctl(userfault_fd, UFFDIO_ZEROPAGE, &zero_struct);
    }
    if (!ret) {
        ramblock_recv_bitmap_set_range(rb, host_addr,
                                       pagesize / qemu_target_page_size());
    }
    return ret;
}

int postcopy_notify_shared_wake(RAMBlock *rb, uint64_t offset)
{
    int i;
    MigrationIncomingState *mis = migration_incoming_get_current();
    GArray *pcrfds = mis->postcopy_remote_fds;

    for (i = 0; i < pcrfds->len; i++) {
        struct PostCopyFD *cur = &g_array_index(pcrfds, struct PostCopyFD, i);
        int ret = cur->waker(cur, rb, offset);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

/*
 * Place a host page (from) at (host) atomically
 * returns 0 on success
 */
int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);

    /* copy also acks to the kernel waking the stalled thread up
     * TODO: We can inhibit that ack and only do it if it was requested
     * which would be slightly cheaper, but we'd have to be careful
     * of the order of updating our page state.
     */
    if (qemu_ufd_copy_ioctl(mis->userfault_fd, host, from, pagesize, rb)) {
        int e = errno;
        error_report("%s: %s copy host: %p from: %p (size: %zd)",
                     __func__, strerror(e), host, from, pagesize);

        return -e;
    }

    trace_postcopy_place_page(host);
    return postcopy_notify_shared_wake(rb,
                                       qemu_ram_block_host_offset(rb, host));
}

/*
 * Place a zero page at (host) atomically
 * returns 0 on success
 */
int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                             RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    trace_postcopy_place_page_zero(host);

    /* Normal RAMBlocks can zero a page using UFFDIO_ZEROPAGE
     * but it's not available for everything (e.g. hugetlbpages)
     */
    if (qemu_ram_is_uf_zeroable(rb)) {
        if (qemu_ufd_copy_ioctl(mis->userfault_fd, host, NULL, pagesize, rb)) {
            int e = errno;
            error_report("%s: %s zero host: %p",
                         __func__, strerror(e), host);

            return -e;
        }
        return postcopy_notify_shared_wake(rb,
                                           qemu_ram_block_host_offset(rb,
                                                                      host));
    } else {
        /* The kernel can't use UFFDIO_ZEROPAGE for hugepages */
        if (!mis->postcopy_tmp_zero_page) {
            mis->postcopy_tmp_zero_page = mmap(NULL, mis->largest_page_size,
                                               PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANONYMOUS,
                                               -1, 0);
            if (mis->postcopy_tmp_zero_page == MAP_FAILED) {
                int e = errno;
                mis->postcopy_tmp_zero_page = NULL;
                error_report("%s: %s mapping large zero page",
                             __func__, strerror(e));
                return -e;
            }
            memset(mis->postcopy_tmp_zero_page, '\0', mis->largest_page_size);
        }
        return postcopy_place_page(mis, host, mis->postcopy_tmp_zero_page,
                                   rb);
    }
}

/*
 * Returns a target page of memory that can be mapped at a later point in time
 * using postcopy_place_page
 * The same address is used repeatedly, postcopy_place_page just takes the
 * backing page away.
 * Returns: Pointer to allocated page
 *
 */
void *postcopy_get_tmp_page(MigrationIncomingState *mis)
{
    if (!mis->postcopy_tmp_page) {
        mis->postcopy_tmp_page = mmap(NULL, mis->largest_page_size,
                             PROT_READ | PROT_WRITE, MAP_PRIVATE |
                             MAP_ANONYMOUS, -1, 0);
        if (mis->postcopy_tmp_page == MAP_FAILED) {
            mis->postcopy_tmp_page = NULL;
            error_report("%s: %s", __func__, strerror(errno));
            return NULL;
        }
    }

    return mis->postcopy_tmp_page;
}

#else
/* No target OS support, stubs just fail */
bool postcopy_ram_supported_by_host(MigrationIncomingState *mis)
{
    error_report("%s: No OS support", __func__);
    return false;
}

int postcopy_ram_incoming_init(MigrationIncomingState *mis, size_t ram_pages)
{
    error_report("postcopy_ram_incoming_init: No OS support");
    return -1;
}

int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    assert(0);
    return -1;
}

int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    assert(0);
    return -1;
}

int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    assert(0);
    return -1;
}

int postcopy_ram_enable_notify(MigrationIncomingState *mis)
{
    assert(0);
    return -1;
}

int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    assert(0);
    return -1;
}

int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                        RAMBlock *rb)
{
    assert(0);
    return -1;
}

void *postcopy_get_tmp_page(MigrationIncomingState *mis)
{
    assert(0);
    return NULL;
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    assert(0);
    return -1;
}
#endif

/* ------------------------------------------------------------------------- */

void postcopy_fault_thread_notify(MigrationIncomingState *mis)
{
    uint64_t tmp64 = 1;

    /*
     * Wakeup the fault_thread.  It's an eventfd that should currently
     * be at 0, we're going to increment it to 1
     */
    if (write(mis->userfault_event_fd, &tmp64, 8) != 8) {
        /* Not much we can do here, but may as well report it */
        error_report("%s: incrementing failed: %s", __func__,
                     strerror(errno));
    }
}

/**
 * postcopy_discard_send_init: Called at the start of each RAMBlock before
 *   asking to discard individual ranges.
 *
 * @ms: The current migration state.
 * @offset: the bitmap offset of the named RAMBlock in the migration
 *   bitmap.
 * @name: RAMBlock that discards will operate on.
 *
 * returns: a new PDS.
 */
PostcopyDiscardState *postcopy_discard_send_init(MigrationState *ms,
                                                 const char *name)
{
    PostcopyDiscardState *res = g_malloc0(sizeof(PostcopyDiscardState));

    if (res) {
        res->ramblock_name = name;
    }

    return res;
}

/**
 * postcopy_discard_send_range: Called by the bitmap code for each chunk to
 *   discard. May send a discard message, may just leave it queued to
 *   be sent later.
 *
 * @ms: Current migration state.
 * @pds: Structure initialised by postcopy_discard_send_init().
 * @start,@length: a range of pages in the migration bitmap in the
 *   RAM block passed to postcopy_discard_send_init() (length=1 is one page)
 */
void postcopy_discard_send_range(MigrationState *ms, PostcopyDiscardState *pds,
                                unsigned long start, unsigned long length)
{
    size_t tp_size = qemu_target_page_size();
    /* Convert to byte offsets within the RAM block */
    pds->start_list[pds->cur_entry] = start  * tp_size;
    pds->length_list[pds->cur_entry] = length * tp_size;
    trace_postcopy_discard_send_range(pds->ramblock_name, start, length);
    pds->cur_entry++;
    pds->nsentwords++;

    if (pds->cur_entry == MAX_DISCARDS_PER_COMMAND) {
        /* Full set, ship it! */
        qemu_savevm_send_postcopy_ram_discard(ms->to_dst_file,
                                              pds->ramblock_name,
                                              pds->cur_entry,
                                              pds->start_list,
                                              pds->length_list);
        pds->nsentcmds++;
        pds->cur_entry = 0;
    }
}

/**
 * postcopy_discard_send_finish: Called at the end of each RAMBlock by the
 * bitmap code. Sends any outstanding discard messages, frees the PDS
 *
 * @ms: Current migration state.
 * @pds: Structure initialised by postcopy_discard_send_init().
 */
void postcopy_discard_send_finish(MigrationState *ms, PostcopyDiscardState *pds)
{
    /* Anything unsent? */
    if (pds->cur_entry) {
        qemu_savevm_send_postcopy_ram_discard(ms->to_dst_file,
                                              pds->ramblock_name,
                                              pds->cur_entry,
                                              pds->start_list,
                                              pds->length_list);
        pds->nsentcmds++;
    }

    trace_postcopy_discard_send_finish(pds->ramblock_name, pds->nsentwords,
                                       pds->nsentcmds);

    g_free(pds);
}

/*
 * Current state of incoming postcopy; note this is not part of
 * MigrationIncomingState since it's state is used during cleanup
 * at the end as MIS is being freed.
 */
static PostcopyState incoming_postcopy_state;

PostcopyState  postcopy_state_get(void)
{
    return atomic_mb_read(&incoming_postcopy_state);
}

/* Set the state and return the old state */
PostcopyState postcopy_state_set(PostcopyState new_state)
{
    return atomic_xchg(&incoming_postcopy_state, new_state);
}

/* Register a handler for external shared memory postcopy
 * called on the destination.
 */
void postcopy_register_shared_ufd(struct PostCopyFD *pcfd)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    mis->postcopy_remote_fds = g_array_append_val(mis->postcopy_remote_fds,
                                                  *pcfd);
}

/* Unregister a handler for external shared memory postcopy
 */
void postcopy_unregister_shared_ufd(struct PostCopyFD *pcfd)
{
    guint i;
    MigrationIncomingState *mis = migration_incoming_get_current();
    GArray *pcrfds = mis->postcopy_remote_fds;

    for (i = 0; i < pcrfds->len; i++) {
        struct PostCopyFD *cur = &g_array_index(pcrfds, struct PostCopyFD, i);
        if (cur->fd == pcfd->fd) {
            mis->postcopy_remote_fds = g_array_remove_index(pcrfds, i);
            return;
        }
    }
}
