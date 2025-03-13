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
#include "qemu/madvise.h"
#include "exec/target_page.h"
#include "migration.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "ram.h"
#include "qapi/error.h"
#include "qemu/notify.h"
#include "qemu/rcu.h"
#include "system/system.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/boards.h"
#include "system/ramblock.h"
#include "socket.h"
#include "yank_functions.h"
#include "tls.h"
#include "qemu/userfaultfd.h"
#include "qemu/mmap-alloc.h"
#include "options.h"

/* Arbitrary limit on size of each discard command,
 * keeps them around ~200 bytes
 */
#define MAX_DISCARDS_PER_COMMAND 12

typedef struct PostcopyDiscardState {
    const char *ramblock_name;
    uint16_t cur_entry;
    /*
     * Start and length of a discard range (bytes)
     */
    uint64_t start_list[MAX_DISCARDS_PER_COMMAND];
    uint64_t length_list[MAX_DISCARDS_PER_COMMAND];
    unsigned int nsentwords;
    unsigned int nsentcmds;
} PostcopyDiscardState;

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

    return notifier_with_return_list_notify(&postcopy_notifier_list,
                                            &pnd, errp);
}

/*
 * NOTE: this routine is not thread safe, we can't call it concurrently. But it
 * should be good enough for migration's purposes.
 */
void postcopy_thread_create(MigrationIncomingState *mis,
                            QemuThread *thread, const char *name,
                            void *(*fn)(void *), int joinable)
{
    qemu_sem_init(&mis->thread_sync_sem, 0);
    qemu_thread_create(thread, name, fn, mis, joinable);
    qemu_sem_wait(&mis->thread_sync_sem);
    qemu_sem_destroy(&mis->thread_sync_sem);
}

/* Postcopy needs to detect accesses to pages that haven't yet been copied
 * across, and efficiently map new pages in, the techniques for doing this
 * are target OS specific.
 */
#if defined(__linux__)
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>

typedef struct PostcopyBlocktimeContext {
    /* time when page fault initiated per vCPU */
    uint32_t *page_fault_vcpu_time;
    /* page address per vCPU */
    uintptr_t *vcpu_addr;
    uint32_t total_blocktime;
    /* blocktime per vCPU */
    uint32_t *vcpu_blocktime;
    /* point in time when last page fault was initiated */
    uint32_t last_begin;
    /* number of vCPU are suspended */
    int smp_cpus_down;
    uint64_t start_time;

    /*
     * Handler for exit event, necessary for
     * releasing whole blocktime_ctx
     */
    Notifier exit_notifier;
} PostcopyBlocktimeContext;

static void destroy_blocktime_context(struct PostcopyBlocktimeContext *ctx)
{
    g_free(ctx->page_fault_vcpu_time);
    g_free(ctx->vcpu_addr);
    g_free(ctx->vcpu_blocktime);
    g_free(ctx);
}

static void migration_exit_cb(Notifier *n, void *data)
{
    PostcopyBlocktimeContext *ctx = container_of(n, PostcopyBlocktimeContext,
                                                 exit_notifier);
    destroy_blocktime_context(ctx);
}

static struct PostcopyBlocktimeContext *blocktime_context_new(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    PostcopyBlocktimeContext *ctx = g_new0(PostcopyBlocktimeContext, 1);
    ctx->page_fault_vcpu_time = g_new0(uint32_t, smp_cpus);
    ctx->vcpu_addr = g_new0(uintptr_t, smp_cpus);
    ctx->vcpu_blocktime = g_new0(uint32_t, smp_cpus);

    ctx->exit_notifier.notify = migration_exit_cb;
    ctx->start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    qemu_add_exit_notifier(&ctx->exit_notifier);
    return ctx;
}

static uint32List *get_vcpu_blocktime_list(PostcopyBlocktimeContext *ctx)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    uint32List *list = NULL;
    int i;

    for (i = ms->smp.cpus - 1; i >= 0; i--) {
        QAPI_LIST_PREPEND(list, ctx->vcpu_blocktime[i]);
    }

    return list;
}

/*
 * This function just populates MigrationInfo from postcopy's
 * blocktime context. It will not populate MigrationInfo,
 * unless postcopy-blocktime capability was set.
 *
 * @info: pointer to MigrationInfo to populate
 */
void fill_destination_postcopy_migration_info(MigrationInfo *info)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *bc = mis->blocktime_ctx;

    if (!bc) {
        return;
    }

    info->has_postcopy_blocktime = true;
    info->postcopy_blocktime = bc->total_blocktime;
    info->has_postcopy_vcpu_blocktime = true;
    info->postcopy_vcpu_blocktime = get_vcpu_blocktime_list(bc);
}

static uint32_t get_postcopy_total_blocktime(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *bc = mis->blocktime_ctx;

    if (!bc) {
        return 0;
    }

    return bc->total_blocktime;
}

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

    ufd = uffd_open(O_CLOEXEC);
    if (ufd == -1) {
        error_report("%s: uffd_open() failed: %s", __func__, strerror(errno));
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
 * Returns: true on success
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

    ioctl_mask = 1ULL << _UFFDIO_REGISTER |
                 1ULL << _UFFDIO_UNREGISTER;
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        error_report("Missing userfault features: %" PRIx64,
                     (uint64_t)(~api_struct.ioctls & ioctl_mask));
        return false;
    }

    return true;
}

static bool ufd_check_and_apply(int ufd, MigrationIncomingState *mis,
                                Error **errp)
{
    ERRP_GUARD();
    uint64_t asked_features = 0;
    static uint64_t supported_features;

    /*
     * it's not possible to
     * request UFFD_API twice per one fd
     * userfault fd features is persistent
     */
    if (!supported_features) {
        if (!receive_ufd_features(&supported_features)) {
            error_setg(errp, "Userfault feature detection failed");
            return false;
        }
    }

#ifdef UFFD_FEATURE_THREAD_ID
    if (UFFD_FEATURE_THREAD_ID & supported_features) {
        asked_features |= UFFD_FEATURE_THREAD_ID;
        if (migrate_postcopy_blocktime()) {
            if (!mis->blocktime_ctx) {
                mis->blocktime_ctx = blocktime_context_new();
            }
        }
    }
#endif

    /*
     * request features, even if asked_features is 0, due to
     * kernel expects UFFD_API before UFFDIO_REGISTER, per
     * userfault file descriptor
     */
    if (!request_ufd_features(ufd, asked_features)) {
        error_setg(errp, "Failed features %" PRIu64, asked_features);
        return false;
    }

    if (qemu_real_host_page_size() != ram_pagesize_summary()) {
        bool have_hp = false;
        /* We've got a huge page */
#ifdef UFFD_FEATURE_MISSING_HUGETLBFS
        have_hp = supported_features & UFFD_FEATURE_MISSING_HUGETLBFS;
#endif
        if (!have_hp) {
            error_setg(errp,
                       "Userfault on this host does not support huge pages");
            return false;
        }
    }
    return true;
}

/* Callback from postcopy_ram_supported_by_host block iterator.
 */
static int test_ramblock_postcopiable(RAMBlock *rb, Error **errp)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    size_t pagesize = qemu_ram_pagesize(rb);
    QemuFsType fs;

    if (length % pagesize) {
        error_setg(errp,
                   "Postcopy requires RAM blocks to be a page size multiple,"
                   " block %s is 0x" RAM_ADDR_FMT " bytes with a "
                   "page size of 0x%zx", block_name, length, pagesize);
        return 1;
    }

    if (rb->fd >= 0) {
        fs = qemu_fd_getfs(rb->fd);
        if (fs != QEMU_FS_TYPE_TMPFS && fs != QEMU_FS_TYPE_HUGETLBFS) {
            error_setg(errp,
                       "Host backend files need to be TMPFS or HUGETLBFS only");
            return 1;
        }
    }

    return 0;
}

/*
 * Note: This has the side effect of munlock'ing all of RAM, that's
 * normally fine since if the postcopy succeeds it gets turned back on at the
 * end.
 */
bool postcopy_ram_supported_by_host(MigrationIncomingState *mis, Error **errp)
{
    ERRP_GUARD();
    long pagesize = qemu_real_host_page_size();
    int ufd = -1;
    bool ret = false; /* Error unless we change it */
    void *testarea = NULL;
    struct uffdio_register reg_struct;
    struct uffdio_range range_struct;
    uint64_t feature_mask;
    RAMBlock *block;

    if (qemu_target_page_size() > pagesize) {
        error_setg(errp, "Target page size bigger than host page size");
        goto out;
    }

    ufd = uffd_open(O_CLOEXEC);
    if (ufd == -1) {
        error_setg(errp, "Userfaultfd not available: %s", strerror(errno));
        goto out;
    }

    /* Give devices a chance to object */
    if (postcopy_notify(POSTCOPY_NOTIFY_PROBE, errp)) {
        goto out;
    }

    /* Version and features check */
    if (!ufd_check_and_apply(ufd, mis, errp)) {
        goto out;
    }

    /*
     * We don't support postcopy with some type of ramblocks.
     *
     * NOTE: we explicitly ignored migrate_ram_is_ignored() instead we checked
     * all possible ramblocks.  This is because this function can be called
     * when creating the migration object, during the phase RAM_MIGRATABLE
     * is not even properly set for all the ramblocks.
     *
     * A side effect of this is we'll also check against RAM_SHARED
     * ramblocks even if migrate_ignore_shared() is set (in which case
     * we'll never migrate RAM_SHARED at all), but normally this shouldn't
     * affect in reality, or we can revisit.
     */
    RAMBLOCK_FOREACH(block) {
        if (test_ramblock_postcopiable(block, errp)) {
            goto out;
        }
    }

    /*
     * userfault and mlock don't go together; we'll put it back later if
     * it was enabled.
     */
    if (munlockall()) {
        error_setg(errp, "munlockall() failed: %s", strerror(errno));
        goto out;
    }

    /*
     *  We need to check that the ops we need are supported on anon memory
     *  To do that we need to register a chunk and see the flags that
     *  are returned.
     */
    testarea = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE |
                                    MAP_ANONYMOUS, -1, 0);
    if (testarea == MAP_FAILED) {
        error_setg(errp, "Failed to map test area: %s", strerror(errno));
        goto out;
    }
    g_assert(QEMU_PTR_IS_ALIGNED(testarea, pagesize));

    reg_struct.range.start = (uintptr_t)testarea;
    reg_struct.range.len = pagesize;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    if (ioctl(ufd, UFFDIO_REGISTER, &reg_struct)) {
        error_setg(errp, "UFFDIO_REGISTER failed: %s", strerror(errno));
        goto out;
    }

    range_struct.start = (uintptr_t)testarea;
    range_struct.len = pagesize;
    if (ioctl(ufd, UFFDIO_UNREGISTER, &range_struct)) {
        error_setg(errp, "UFFDIO_UNREGISTER failed: %s", strerror(errno));
        goto out;
    }

    feature_mask = 1ULL << _UFFDIO_WAKE |
                   1ULL << _UFFDIO_COPY |
                   1ULL << _UFFDIO_ZEROPAGE;
    if ((reg_struct.ioctls & feature_mask) != feature_mask) {
        error_setg(errp, "Missing userfault map features: %" PRIx64,
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
static int init_range(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    trace_postcopy_init_range(block_name, host_addr, offset, length);

    /*
     * Save the used_length before running the guest. In case we have to
     * resize RAM blocks when syncing RAM block sizes from the source during
     * precopy, we'll update it manually via the ram block notifier.
     */
    rb->postcopy_length = length;

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
static int cleanup_range(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = rb->postcopy_length;
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
int postcopy_ram_incoming_init(MigrationIncomingState *mis)
{
    if (foreach_not_ignored_block(init_range, NULL)) {
        return -1;
    }

    return 0;
}

static void postcopy_temp_pages_cleanup(MigrationIncomingState *mis)
{
    int i;

    if (mis->postcopy_tmp_pages) {
        for (i = 0; i < mis->postcopy_channels; i++) {
            if (mis->postcopy_tmp_pages[i].tmp_huge_page) {
                munmap(mis->postcopy_tmp_pages[i].tmp_huge_page,
                       mis->largest_page_size);
                mis->postcopy_tmp_pages[i].tmp_huge_page = NULL;
            }
        }
        g_free(mis->postcopy_tmp_pages);
        mis->postcopy_tmp_pages = NULL;
    }

    if (mis->postcopy_tmp_zero_page) {
        munmap(mis->postcopy_tmp_zero_page, mis->largest_page_size);
        mis->postcopy_tmp_zero_page = NULL;
    }
}

/*
 * At the end of a migration where postcopy_ram_incoming_init was called.
 */
int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    trace_postcopy_ram_incoming_cleanup_entry();

    if (mis->preempt_thread_status == PREEMPT_THREAD_CREATED) {
        /* Notify the fast load thread to quit */
        mis->preempt_thread_status = PREEMPT_THREAD_QUIT;
        /*
         * Update preempt_thread_status before reading count.  Note: mutex
         * lock only provide ACQUIRE semantic, and it doesn't stops this
         * write to be reordered after reading the count.
         */
        smp_mb();
        /*
         * It's possible that the preempt thread is still handling the last
         * pages to arrive which were requested by guest page faults.
         * Making sure nothing is left behind by waiting on the condvar if
         * that unlikely case happened.
         */
        WITH_QEMU_LOCK_GUARD(&mis->page_request_mutex) {
            if (qatomic_read(&mis->page_requested_count)) {
                /*
                 * It is guaranteed to receive a signal later, because the
                 * count>0 now, so it's destined to be decreased to zero
                 * very soon by the preempt thread.
                 */
                qemu_cond_wait(&mis->page_request_cond,
                               &mis->page_request_mutex);
            }
        }
        /* Notify the fast load thread to quit */
        if (mis->postcopy_qemufile_dst) {
            qemu_file_shutdown(mis->postcopy_qemufile_dst);
        }
        qemu_thread_join(&mis->postcopy_prio_thread);
        mis->preempt_thread_status = PREEMPT_THREAD_NONE;
    }

    if (mis->have_fault_thread) {
        Error *local_err = NULL;

        /* Let the fault thread quit */
        qatomic_set(&mis->fault_thread_quit, 1);
        postcopy_fault_thread_notify(mis);
        trace_postcopy_ram_incoming_cleanup_join();
        qemu_thread_join(&mis->fault_thread);

        if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_END, &local_err)) {
            error_report_err(local_err);
            return -1;
        }

        if (foreach_not_ignored_block(cleanup_range, mis)) {
            return -1;
        }

        trace_postcopy_ram_incoming_cleanup_closeuf();
        close(mis->userfault_fd);
        close(mis->userfault_event_fd);
        mis->have_fault_thread = false;
    }

    if (should_mlock(mlock_state)) {
        if (os_mlock(is_mlock_on_fault(mlock_state)) < 0) {
            error_report("mlock: %s", strerror(errno));
            /*
             * It doesn't feel right to fail at this point, we have a valid
             * VM state.
             */
        }
    }

    postcopy_temp_pages_cleanup(mis);

    trace_postcopy_ram_incoming_cleanup_blocktime(
            get_postcopy_total_blocktime());

    trace_postcopy_ram_incoming_cleanup_exit();
    return 0;
}

/*
 * Disable huge pages on an area
 */
static int nhp_range(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = rb->postcopy_length;
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
    if (foreach_not_ignored_block(nhp_range, mis)) {
        return -1;
    }

    postcopy_state_set(POSTCOPY_INCOMING_DISCARD);

    return 0;
}

/*
 * Mark the given area of RAM as requiring notification to unwritten areas
 * Used as a  callback on foreach_not_ignored_block.
 *   host_addr: Base of area to mark
 *   offset: Offset in the whole ram arena
 *   length: Length of the section
 *   opaque: MigrationIncomingState pointer
 * Returns 0 on success
 */
static int ram_block_enable_notify(RAMBlock *rb, void *opaque)
{
    MigrationIncomingState *mis = opaque;
    struct uffdio_register reg_struct;

    reg_struct.range.start = (uintptr_t)qemu_ram_get_host_addr(rb);
    reg_struct.range.len = rb->postcopy_length;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    /* Now tell our userfault_fd that it's responsible for this area */
    if (ioctl(mis->userfault_fd, UFFDIO_REGISTER, &reg_struct)) {
        error_report("%s userfault register: %s", __func__, strerror(errno));
        return -1;
    }
    if (!(reg_struct.ioctls & (1ULL << _UFFDIO_COPY))) {
        error_report("%s userfault: Region doesn't support COPY", __func__);
        return -1;
    }
    if (reg_struct.ioctls & (1ULL << _UFFDIO_ZEROPAGE)) {
        qemu_ram_set_uf_zeroable(rb);
    }

    return 0;
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    trace_postcopy_wake_shared(client_addr, qemu_ram_get_idstr(rb));
    return uffd_wakeup(pcfd->fd,
                       (void *)(uintptr_t)ROUND_DOWN(client_addr, pagesize),
                       pagesize);
}

static int postcopy_request_page(MigrationIncomingState *mis, RAMBlock *rb,
                                 ram_addr_t start, uint64_t haddr)
{
    void *aligned = (void *)(uintptr_t)ROUND_DOWN(haddr, qemu_ram_pagesize(rb));

    /*
     * Discarded pages (via RamDiscardManager) are never migrated. On unlikely
     * access, place a zeropage, which will also set the relevant bits in the
     * recv_bitmap accordingly, so we won't try placing a zeropage twice.
     *
     * Checking a single bit is sufficient to handle pagesize > TPS as either
     * all relevant bits are set or not.
     */
    assert(QEMU_IS_ALIGNED(start, qemu_ram_pagesize(rb)));
    if (ramblock_page_is_discarded(rb, start)) {
        bool received = ramblock_recv_bitmap_test_byte_offset(rb, start);

        return received ? 0 : postcopy_place_page_zero(mis, aligned, rb);
    }

    return migrate_send_rp_req_pages(mis, rb, start, haddr);
}

/*
 * Callback from shared fault handlers to ask for a page,
 * the page must be specified by a RAMBlock and an offset in that rb
 * Note: Only for use by shared fault handlers (in fault thread)
 */
int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    uint64_t aligned_rbo = ROUND_DOWN(rb_offset, qemu_ram_pagesize(rb));
    MigrationIncomingState *mis = migration_incoming_get_current();

    trace_postcopy_request_shared_page(pcfd->idstr, qemu_ram_get_idstr(rb),
                                       rb_offset);
    if (ramblock_recv_bitmap_test_byte_offset(rb, aligned_rbo)) {
        trace_postcopy_request_shared_page_present(pcfd->idstr,
                                        qemu_ram_get_idstr(rb), rb_offset);
        return postcopy_wake_shared(pcfd, client_addr, rb);
    }
    postcopy_request_page(mis, rb, aligned_rbo, client_addr);
    return 0;
}

static int get_mem_fault_cpu_index(uint32_t pid)
{
    CPUState *cpu_iter;

    CPU_FOREACH(cpu_iter) {
        if (cpu_iter->thread_id == pid) {
            trace_get_mem_fault_cpu_index(cpu_iter->cpu_index, pid);
            return cpu_iter->cpu_index;
        }
    }
    trace_get_mem_fault_cpu_index(-1, pid);
    return -1;
}

static uint32_t get_low_time_offset(PostcopyBlocktimeContext *dc)
{
    int64_t start_time_offset = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) -
                                    dc->start_time;
    return start_time_offset < 1 ? 1 : start_time_offset & UINT32_MAX;
}

/*
 * This function is being called when pagefault occurs. It
 * tracks down vCPU blocking time.
 *
 * @addr: faulted host virtual address
 * @ptid: faulted process thread id
 * @rb: ramblock appropriate to addr
 */
static void mark_postcopy_blocktime_begin(uintptr_t addr, uint32_t ptid,
                                          RAMBlock *rb)
{
    int cpu, already_received;
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *dc = mis->blocktime_ctx;
    uint32_t low_time_offset;

    if (!dc || ptid == 0) {
        return;
    }
    cpu = get_mem_fault_cpu_index(ptid);
    if (cpu < 0) {
        return;
    }

    low_time_offset = get_low_time_offset(dc);
    if (dc->vcpu_addr[cpu] == 0) {
        qatomic_inc(&dc->smp_cpus_down);
    }

    qatomic_xchg(&dc->last_begin, low_time_offset);
    qatomic_xchg(&dc->page_fault_vcpu_time[cpu], low_time_offset);
    qatomic_xchg(&dc->vcpu_addr[cpu], addr);

    /*
     * check it here, not at the beginning of the function,
     * due to, check could occur early than bitmap_set in
     * qemu_ufd_copy_ioctl
     */
    already_received = ramblock_recv_bitmap_test(rb, (void *)addr);
    if (already_received) {
        qatomic_xchg(&dc->vcpu_addr[cpu], 0);
        qatomic_xchg(&dc->page_fault_vcpu_time[cpu], 0);
        qatomic_dec(&dc->smp_cpus_down);
    }
    trace_mark_postcopy_blocktime_begin(addr, dc, dc->page_fault_vcpu_time[cpu],
                                        cpu, already_received);
}

/*
 *  This function just provide calculated blocktime per cpu and trace it.
 *  Total blocktime is calculated in mark_postcopy_blocktime_end.
 *
 *
 * Assume we have 3 CPU
 *
 *      S1        E1           S1               E1
 * -----***********------------xxx***************------------------------> CPU1
 *
 *             S2                E2
 * ------------****************xxx---------------------------------------> CPU2
 *
 *                         S3            E3
 * ------------------------****xxx********-------------------------------> CPU3
 *
 * We have sequence S1,S2,E1,S3,S1,E2,E3,E1
 * S2,E1 - doesn't match condition due to sequence S1,S2,E1 doesn't include CPU3
 * S3,S1,E2 - sequence includes all CPUs, in this case overlap will be S1,E2 -
 *            it's a part of total blocktime.
 * S1 - here is last_begin
 * Legend of the picture is following:
 *              * - means blocktime per vCPU
 *              x - means overlapped blocktime (total blocktime)
 *
 * @addr: host virtual address
 */
static void mark_postcopy_blocktime_end(uintptr_t addr)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *dc = mis->blocktime_ctx;
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    int i, affected_cpu = 0;
    bool vcpu_total_blocktime = false;
    uint32_t read_vcpu_time, low_time_offset;

    if (!dc) {
        return;
    }

    low_time_offset = get_low_time_offset(dc);
    /* lookup cpu, to clear it,
     * that algorithm looks straightforward, but it's not
     * optimal, more optimal algorithm is keeping tree or hash
     * where key is address value is a list of  */
    for (i = 0; i < smp_cpus; i++) {
        uint32_t vcpu_blocktime = 0;

        read_vcpu_time = qatomic_fetch_add(&dc->page_fault_vcpu_time[i], 0);
        if (qatomic_fetch_add(&dc->vcpu_addr[i], 0) != addr ||
            read_vcpu_time == 0) {
            continue;
        }
        qatomic_xchg(&dc->vcpu_addr[i], 0);
        vcpu_blocktime = low_time_offset - read_vcpu_time;
        affected_cpu += 1;
        /* we need to know is that mark_postcopy_end was due to
         * faulted page, another possible case it's prefetched
         * page and in that case we shouldn't be here */
        if (!vcpu_total_blocktime &&
            qatomic_fetch_add(&dc->smp_cpus_down, 0) == smp_cpus) {
            vcpu_total_blocktime = true;
        }
        /* continue cycle, due to one page could affect several vCPUs */
        dc->vcpu_blocktime[i] += vcpu_blocktime;
    }

    qatomic_sub(&dc->smp_cpus_down, affected_cpu);
    if (vcpu_total_blocktime) {
        dc->total_blocktime += low_time_offset - qatomic_fetch_add(
                &dc->last_begin, 0);
    }
    trace_mark_postcopy_blocktime_end(addr, dc, dc->total_blocktime,
                                      affected_cpu);
}

static void postcopy_pause_fault_thread(MigrationIncomingState *mis)
{
    trace_postcopy_pause_fault_thread();
    qemu_sem_wait(&mis->postcopy_pause_sem_fault);
    trace_postcopy_pause_fault_thread_continued();
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
    rcu_register_thread();
    mis->last_rb = NULL; /* last RAMBlock we sent part of */
    qemu_sem_post(&mis->thread_sync_sem);

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

        if (!mis->to_src_file) {
            /*
             * Possibly someone tells us that the return path is
             * broken already using the event. We should hold until
             * the channel is rebuilt.
             */
            postcopy_pause_fault_thread(mis);
        }

        if (pfd[1].revents) {
            uint64_t tmp64 = 0;

            /* Consume the signal */
            if (read(mis->userfault_event_fd, &tmp64, 8) != 8) {
                /* Nothing obviously nicer than posting this error. */
                error_report("%s: read() failed", __func__);
            }

            if (qatomic_read(&mis->fault_thread_quit)) {
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

            rb_offset = ROUND_DOWN(rb_offset, qemu_ram_pagesize(rb));
            trace_postcopy_ram_fault_thread_request(msg.arg.pagefault.address,
                                                qemu_ram_get_idstr(rb),
                                                rb_offset,
                                                msg.arg.pagefault.feat.ptid);
            mark_postcopy_blocktime_begin(
                    (uintptr_t)(msg.arg.pagefault.address),
                                msg.arg.pagefault.feat.ptid, rb);

retry:
            /*
             * Send the request to the source - we want to request one
             * of our host page sizes (which is >= TPS)
             */
            ret = postcopy_request_page(mis, rb, rb_offset,
                                        msg.arg.pagefault.address);
            if (ret) {
                /* May be network failure, try to wait for recovery */
                postcopy_pause_fault_thread(mis);
                goto retry;
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
    rcu_unregister_thread();
    trace_postcopy_ram_fault_thread_exit();
    g_free(pfd);
    return NULL;
}

static int postcopy_temp_pages_setup(MigrationIncomingState *mis)
{
    PostcopyTmpPage *tmp_page;
    int err, i, channels;
    void *temp_page;

    if (migrate_postcopy_preempt()) {
        /* If preemption enabled, need extra channel for urgent requests */
        mis->postcopy_channels = RAM_CHANNEL_MAX;
    } else {
        /* Both precopy/postcopy on the same channel */
        mis->postcopy_channels = 1;
    }

    channels = mis->postcopy_channels;
    mis->postcopy_tmp_pages = g_malloc0_n(sizeof(PostcopyTmpPage), channels);

    for (i = 0; i < channels; i++) {
        tmp_page = &mis->postcopy_tmp_pages[i];
        temp_page = mmap(NULL, mis->largest_page_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (temp_page == MAP_FAILED) {
            err = errno;
            error_report("%s: Failed to map postcopy_tmp_pages[%d]: %s",
                         __func__, i, strerror(err));
            /* Clean up will be done later */
            return -err;
        }
        tmp_page->tmp_huge_page = temp_page;
        /* Initialize default states for each tmp page */
        postcopy_temp_page_reset(tmp_page);
    }

    /*
     * Map large zero page when kernel can't use UFFDIO_ZEROPAGE for hugepages
     */
    mis->postcopy_tmp_zero_page = mmap(NULL, mis->largest_page_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mis->postcopy_tmp_zero_page == MAP_FAILED) {
        err = errno;
        mis->postcopy_tmp_zero_page = NULL;
        error_report("%s: Failed to map large zero page %s",
                     __func__, strerror(err));
        return -err;
    }

    memset(mis->postcopy_tmp_zero_page, '\0', mis->largest_page_size);

    return 0;
}

int postcopy_ram_incoming_setup(MigrationIncomingState *mis)
{
    Error *local_err = NULL;

    /* Open the fd for the kernel to give us userfaults */
    mis->userfault_fd = uffd_open(O_CLOEXEC | O_NONBLOCK);
    if (mis->userfault_fd == -1) {
        error_report("%s: Failed to open userfault fd: %s", __func__,
                     strerror(errno));
        return -1;
    }

    /*
     * Although the host check already tested the API, we need to
     * do the check again as an ABI handshake on the new fd.
     */
    if (!ufd_check_and_apply(mis->userfault_fd, mis, &local_err)) {
        error_report_err(local_err);
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

    postcopy_thread_create(mis, &mis->fault_thread,
                           MIGRATION_THREAD_DST_FAULT,
                           postcopy_ram_fault_thread, QEMU_THREAD_JOINABLE);
    mis->have_fault_thread = true;

    /* Mark so that we get notified of accesses to unwritten areas */
    if (foreach_not_ignored_block(ram_block_enable_notify, mis)) {
        error_report("ram_block_enable_notify failed");
        return -1;
    }

    if (postcopy_temp_pages_setup(mis)) {
        /* Error dumped in the sub-function */
        return -1;
    }

    if (migrate_postcopy_preempt()) {
        /*
         * This thread needs to be created after the temp pages because
         * it'll fetch RAM_CHANNEL_POSTCOPY PostcopyTmpPage immediately.
         */
        postcopy_thread_create(mis, &mis->postcopy_prio_thread,
                               MIGRATION_THREAD_DST_PREEMPT,
                               postcopy_preempt_thread, QEMU_THREAD_JOINABLE);
        mis->preempt_thread_status = PREEMPT_THREAD_CREATED;
    }

    trace_postcopy_ram_enable_notify();

    return 0;
}

static int qemu_ufd_copy_ioctl(MigrationIncomingState *mis, void *host_addr,
                               void *from_addr, uint64_t pagesize, RAMBlock *rb)
{
    int userfault_fd = mis->userfault_fd;
    int ret;

    if (from_addr) {
        ret = uffd_copy_page(userfault_fd, host_addr, from_addr, pagesize,
                             false);
    } else {
        ret = uffd_zero_page(userfault_fd, host_addr, pagesize, false);
    }
    if (!ret) {
        qemu_mutex_lock(&mis->page_request_mutex);
        ramblock_recv_bitmap_set_range(rb, host_addr,
                                       pagesize / qemu_target_page_size());
        /*
         * If this page resolves a page fault for a previous recorded faulted
         * address, take a special note to maintain the requested page list.
         */
        if (g_tree_lookup(mis->page_requested, host_addr)) {
            g_tree_remove(mis->page_requested, host_addr);
            int left_pages = qatomic_dec_fetch(&mis->page_requested_count);

            trace_postcopy_page_req_del(host_addr, mis->page_requested_count);
            /* Order the update of count and read of preempt status */
            smp_mb();
            if (mis->preempt_thread_status == PREEMPT_THREAD_QUIT &&
                left_pages == 0) {
                /*
                 * This probably means the main thread is waiting for us.
                 * Notify that we've finished receiving the last requested
                 * page.
                 */
                qemu_cond_signal(&mis->page_request_cond);
            }
        }
        qemu_mutex_unlock(&mis->page_request_mutex);
        mark_postcopy_blocktime_end((uintptr_t)host_addr);
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
    int e;

    /* copy also acks to the kernel waking the stalled thread up
     * TODO: We can inhibit that ack and only do it if it was requested
     * which would be slightly cheaper, but we'd have to be careful
     * of the order of updating our page state.
     */
    e = qemu_ufd_copy_ioctl(mis, host, from, pagesize, rb);
    if (e) {
        return e;
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
        int e;
        e = qemu_ufd_copy_ioctl(mis, host, NULL, pagesize, rb);
        if (e) {
            return e;
        }
        return postcopy_notify_shared_wake(rb,
                                           qemu_ram_block_host_offset(rb,
                                                                      host));
    } else {
        return postcopy_place_page(mis, host, mis->postcopy_tmp_zero_page, rb);
    }
}

#else
/* No target OS support, stubs just fail */
void fill_destination_postcopy_migration_info(MigrationInfo *info)
{
}

bool postcopy_ram_supported_by_host(MigrationIncomingState *mis, Error **errp)
{
    error_report("%s: No OS support", __func__);
    return false;
}

int postcopy_ram_incoming_init(MigrationIncomingState *mis)
{
    error_report("postcopy_ram_incoming_init: No OS support");
    return -1;
}

int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    g_assert_not_reached();
}

int postcopy_ram_incoming_setup(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    g_assert_not_reached();
}

int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                        RAMBlock *rb)
{
    g_assert_not_reached();
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    g_assert_not_reached();
}
#endif

/* ------------------------------------------------------------------------- */
void postcopy_temp_page_reset(PostcopyTmpPage *tmp_page)
{
    tmp_page->target_pages = 0;
    tmp_page->host_addr = NULL;
    /*
     * This is set to true when reset, and cleared as long as we received any
     * of the non-zero small page within this huge page.
     */
    tmp_page->all_zero = true;
}

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
 * @offset: the bitmap offset of the named RAMBlock in the migration bitmap.
 * @name: RAMBlock that discards will operate on.
 */
static PostcopyDiscardState pds = {0};
void postcopy_discard_send_init(MigrationState *ms, const char *name)
{
    pds.ramblock_name = name;
    pds.cur_entry = 0;
    pds.nsentwords = 0;
    pds.nsentcmds = 0;
}

/**
 * postcopy_discard_send_range: Called by the bitmap code for each chunk to
 *   discard. May send a discard message, may just leave it queued to
 *   be sent later.
 *
 * @ms: Current migration state.
 * @start,@length: a range of pages in the migration bitmap in the
 *   RAM block passed to postcopy_discard_send_init() (length=1 is one page)
 */
void postcopy_discard_send_range(MigrationState *ms, unsigned long start,
                                 unsigned long length)
{
    size_t tp_size = qemu_target_page_size();
    /* Convert to byte offsets within the RAM block */
    pds.start_list[pds.cur_entry] = start  * tp_size;
    pds.length_list[pds.cur_entry] = length * tp_size;
    trace_postcopy_discard_send_range(pds.ramblock_name, start, length);
    pds.cur_entry++;
    pds.nsentwords++;

    if (pds.cur_entry == MAX_DISCARDS_PER_COMMAND) {
        /* Full set, ship it! */
        qemu_savevm_send_postcopy_ram_discard(ms->to_dst_file,
                                              pds.ramblock_name,
                                              pds.cur_entry,
                                              pds.start_list,
                                              pds.length_list);
        pds.nsentcmds++;
        pds.cur_entry = 0;
    }
}

/**
 * postcopy_discard_send_finish: Called at the end of each RAMBlock by the
 * bitmap code. Sends any outstanding discard messages, frees the PDS
 *
 * @ms: Current migration state.
 */
void postcopy_discard_send_finish(MigrationState *ms)
{
    /* Anything unsent? */
    if (pds.cur_entry) {
        qemu_savevm_send_postcopy_ram_discard(ms->to_dst_file,
                                              pds.ramblock_name,
                                              pds.cur_entry,
                                              pds.start_list,
                                              pds.length_list);
        pds.nsentcmds++;
    }

    trace_postcopy_discard_send_finish(pds.ramblock_name, pds.nsentwords,
                                       pds.nsentcmds);
}

/*
 * Current state of incoming postcopy; note this is not part of
 * MigrationIncomingState since it's state is used during cleanup
 * at the end as MIS is being freed.
 */
static PostcopyState incoming_postcopy_state;

PostcopyState  postcopy_state_get(void)
{
    return qatomic_load_acquire(&incoming_postcopy_state);
}

/* Set the state and return the old state */
PostcopyState postcopy_state_set(PostcopyState new_state)
{
    return qatomic_xchg(&incoming_postcopy_state, new_state);
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

    if (!pcrfds) {
        /* migration has already finished and freed the array */
        return;
    }
    for (i = 0; i < pcrfds->len; i++) {
        struct PostCopyFD *cur = &g_array_index(pcrfds, struct PostCopyFD, i);
        if (cur->fd == pcfd->fd) {
            mis->postcopy_remote_fds = g_array_remove_index(pcrfds, i);
            return;
        }
    }
}

void postcopy_preempt_new_channel(MigrationIncomingState *mis, QEMUFile *file)
{
    /*
     * The new loading channel has its own threads, so it needs to be
     * blocked too.  It's by default true, just be explicit.
     */
    qemu_file_set_blocking(file, true);
    mis->postcopy_qemufile_dst = file;
    qemu_sem_post(&mis->postcopy_qemufile_dst_done);
    trace_postcopy_preempt_new_channel();
}

/*
 * Setup the postcopy preempt channel with the IOC.  If ERROR is specified,
 * setup the error instead.  This helper will free the ERROR if specified.
 */
static void
postcopy_preempt_send_channel_done(MigrationState *s,
                                   QIOChannel *ioc, Error *local_err)
{
    if (local_err) {
        migrate_set_error(s, local_err);
        error_free(local_err);
    } else {
        migration_ioc_register_yank(ioc);
        s->postcopy_qemufile_src = qemu_file_new_output(ioc);
        trace_postcopy_preempt_new_channel();
    }

    /*
     * Kick the waiter in all cases.  The waiter should check upon
     * postcopy_qemufile_src to know whether it failed or not.
     */
    qemu_sem_post(&s->postcopy_qemufile_src_sem);
}

static void
postcopy_preempt_tls_handshake(QIOTask *task, gpointer opaque)
{
    g_autoptr(QIOChannel) ioc = QIO_CHANNEL(qio_task_get_source(task));
    MigrationState *s = opaque;
    Error *local_err = NULL;

    qio_task_propagate_error(task, &local_err);
    postcopy_preempt_send_channel_done(s, ioc, local_err);
}

static void
postcopy_preempt_send_channel_new(QIOTask *task, gpointer opaque)
{
    g_autoptr(QIOChannel) ioc = QIO_CHANNEL(qio_task_get_source(task));
    MigrationState *s = opaque;
    QIOChannelTLS *tioc;
    Error *local_err = NULL;

    if (qio_task_propagate_error(task, &local_err)) {
        goto out;
    }

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        tioc = migration_tls_client_create(ioc, s->hostname, &local_err);
        if (!tioc) {
            goto out;
        }
        trace_postcopy_preempt_tls_handshake();
        qio_channel_set_name(QIO_CHANNEL(tioc), "migration-tls-preempt");
        qio_channel_tls_handshake(tioc, postcopy_preempt_tls_handshake,
                                  s, NULL, NULL);
        /* Setup the channel until TLS handshake finished */
        return;
    }

out:
    /* This handles both good and error cases */
    postcopy_preempt_send_channel_done(s, ioc, local_err);
}

/*
 * This function will kick off an async task to establish the preempt
 * channel, and wait until the connection setup completed.  Returns 0 if
 * channel established, -1 for error.
 */
int postcopy_preempt_establish_channel(MigrationState *s)
{
    /* If preempt not enabled, no need to wait */
    if (!migrate_postcopy_preempt()) {
        return 0;
    }

    /*
     * Kick off async task to establish preempt channel.  Only do so with
     * 8.0+ machines, because 7.1/7.2 require the channel to be created in
     * setup phase of migration (even if racy in an unreliable network).
     */
    if (!s->preempt_pre_7_2) {
        postcopy_preempt_setup(s);
    }

    /*
     * We need the postcopy preempt channel to be established before
     * starting doing anything.
     */
    qemu_sem_wait(&s->postcopy_qemufile_src_sem);

    return s->postcopy_qemufile_src ? 0 : -1;
}

void postcopy_preempt_setup(MigrationState *s)
{
    /* Kick an async task to connect */
    socket_send_channel_create(postcopy_preempt_send_channel_new, s);
}

static void postcopy_pause_ram_fast_load(MigrationIncomingState *mis)
{
    trace_postcopy_pause_fast_load();
    qemu_mutex_unlock(&mis->postcopy_prio_thread_mutex);
    qemu_sem_wait(&mis->postcopy_pause_sem_fast_load);
    qemu_mutex_lock(&mis->postcopy_prio_thread_mutex);
    trace_postcopy_pause_fast_load_continued();
}

static bool preempt_thread_should_run(MigrationIncomingState *mis)
{
    return mis->preempt_thread_status != PREEMPT_THREAD_QUIT;
}

void *postcopy_preempt_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    int ret;

    trace_postcopy_preempt_thread_entry();

    rcu_register_thread();

    qemu_sem_post(&mis->thread_sync_sem);

    /*
     * The preempt channel is established in asynchronous way.  Wait
     * for its completion.
     */
    qemu_sem_wait(&mis->postcopy_qemufile_dst_done);

    /* Sending RAM_SAVE_FLAG_EOS to terminate this thread */
    qemu_mutex_lock(&mis->postcopy_prio_thread_mutex);
    while (preempt_thread_should_run(mis)) {
        ret = ram_load_postcopy(mis->postcopy_qemufile_dst,
                                RAM_CHANNEL_POSTCOPY);
        /* If error happened, go into recovery routine */
        if (ret && preempt_thread_should_run(mis)) {
            postcopy_pause_ram_fast_load(mis);
        } else {
            /* We're done */
            break;
        }
    }
    qemu_mutex_unlock(&mis->postcopy_prio_thread_mutex);

    rcu_unregister_thread();

    trace_postcopy_preempt_thread_exit();

    return NULL;
}

bool postcopy_is_paused(MigrationStatus status)
{
    return status == MIGRATION_STATUS_POSTCOPY_PAUSED ||
        status == MIGRATION_STATUS_POSTCOPY_RECOVER_SETUP;
}
