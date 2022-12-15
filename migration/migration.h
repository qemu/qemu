/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_H
#define QEMU_MIGRATION_H

#include "exec/cpu-common.h"
#include "hw/qdev-core.h"
#include "qapi/qapi-types-migration.h"
#include "qemu/thread.h"
#include "qemu/coroutine_int.h"
#include "io/channel.h"
#include "io/channel-buffer.h"
#include "net/announce.h"
#include "qom/object.h"
#include "postcopy-ram.h"

struct PostcopyBlocktimeContext;

#define  MIGRATION_RESUME_ACK_VALUE  (1)

/*
 * 1<<6=64 pages -> 256K chunk when page size is 4K.  This gives us
 * the benefit that all the chunks are 64 pages aligned then the
 * bitmaps are always aligned to LONG.
 */
#define CLEAR_BITMAP_SHIFT_MIN             6
/*
 * 1<<18=256K pages -> 1G chunk when page size is 4K.  This is the
 * default value to use if no one specified.
 */
#define CLEAR_BITMAP_SHIFT_DEFAULT        18
/*
 * 1<<31=2G pages -> 8T chunk when page size is 4K.  This should be
 * big enough and make sure we won't overflow easily.
 */
#define CLEAR_BITMAP_SHIFT_MAX            31

/* This is an abstraction of a "temp huge page" for postcopy's purpose */
typedef struct {
    /*
     * This points to a temporary huge page as a buffer for UFFDIO_COPY.  It's
     * mmap()ed and needs to be freed when cleanup.
     */
    void *tmp_huge_page;
    /*
     * This points to the host page we're going to install for this temp page.
     * It tells us after we've received the whole page, where we should put it.
     */
    void *host_addr;
    /* Number of small pages copied (in size of TARGET_PAGE_SIZE) */
    unsigned int target_pages;
    /* Whether this page contains all zeros */
    bool all_zero;
} PostcopyTmpPage;

/* State for the incoming migration */
struct MigrationIncomingState {
    QEMUFile *from_src_file;
    /* Previously received RAM's RAMBlock pointer */
    RAMBlock *last_recv_block[RAM_CHANNEL_MAX];
    /* A hook to allow cleanup at the end of incoming migration */
    void *transport_data;
    void (*transport_cleanup)(void *data);
    /*
     * Used to sync thread creations.  Note that we can't create threads in
     * parallel with this sem.
     */
    QemuSemaphore  thread_sync_sem;
    /*
     * Free at the start of the main state load, set as the main thread finishes
     * loading state.
     */
    QemuEvent main_thread_load_event;

    /* For network announces */
    AnnounceTimer  announce_timer;

    size_t         largest_page_size;
    bool           have_fault_thread;
    QemuThread     fault_thread;
    /* Set this when we want the fault thread to quit */
    bool           fault_thread_quit;

    bool           have_listen_thread;
    QemuThread     listen_thread;

    /* For the kernel to send us notifications */
    int       userfault_fd;
    /* To notify the fault_thread to wake, e.g., when need to quit */
    int       userfault_event_fd;
    QEMUFile *to_src_file;
    QemuMutex rp_mutex;    /* We send replies from multiple threads */
    /* RAMBlock of last request sent to source */
    RAMBlock *last_rb;
    /*
     * Number of postcopy channels including the default precopy channel, so
     * vanilla postcopy will only contain one channel which contain both
     * precopy and postcopy streams.
     *
     * This is calculated when the src requests to enable postcopy but before
     * it starts.  Its value can depend on e.g. whether postcopy preemption is
     * enabled.
     */
    unsigned int postcopy_channels;
    /* QEMUFile for postcopy only; it'll be handled by a separate thread */
    QEMUFile *postcopy_qemufile_dst;
    /* Postcopy priority thread is used to receive postcopy requested pages */
    QemuThread postcopy_prio_thread;
    bool postcopy_prio_thread_created;
    /*
     * Used to sync between the ram load main thread and the fast ram load
     * thread.  It protects postcopy_qemufile_dst, which is the postcopy
     * fast channel.
     *
     * The ram fast load thread will take it mostly for the whole lifecycle
     * because it needs to continuously read data from the channel, and
     * it'll only release this mutex if postcopy is interrupted, so that
     * the ram load main thread will take this mutex over and properly
     * release the broken channel.
     */
    QemuMutex postcopy_prio_thread_mutex;
    /*
     * An array of temp host huge pages to be used, one for each postcopy
     * channel.
     */
    PostcopyTmpPage *postcopy_tmp_pages;
    /* This is shared for all postcopy channels */
    void     *postcopy_tmp_zero_page;
    /* PostCopyFD's for external userfaultfds & handlers of shared memory */
    GArray   *postcopy_remote_fds;

    QEMUBH *bh;

    int state;

    bool have_colo_incoming_thread;
    QemuThread colo_incoming_thread;
    /* The coroutine we should enter (back) after failover */
    Coroutine *migration_incoming_co;
    QemuSemaphore colo_incoming_sem;

    /*
     * PostcopyBlocktimeContext to keep information for postcopy
     * live migration, to calculate vCPU block time
     * */
    struct PostcopyBlocktimeContext *blocktime_ctx;

    /* notify PAUSED postcopy incoming migrations to try to continue */
    QemuSemaphore postcopy_pause_sem_dst;
    QemuSemaphore postcopy_pause_sem_fault;
    /*
     * This semaphore is used to allow the ram fast load thread (only when
     * postcopy preempt is enabled) fall into sleep when there's network
     * interruption detected.  When the recovery is done, the main load
     * thread will kick the fast ram load thread using this semaphore.
     */
    QemuSemaphore postcopy_pause_sem_fast_load;

    /* List of listening socket addresses  */
    SocketAddressList *socket_address_list;

    /* A tree of pages that we requested to the source VM */
    GTree *page_requested;
    /* For debugging purpose only, but would be nice to keep */
    int page_requested_count;
    /*
     * The mutex helps to maintain the requested pages that we sent to the
     * source, IOW, to guarantee coherent between the page_requests tree and
     * the per-ramblock receivedmap.  Note! This does not guarantee consistency
     * of the real page copy procedures (using UFFDIO_[ZERO]COPY).  E.g., even
     * if one bit in receivedmap is cleared, UFFDIO_COPY could have happened
     * for that page already.  This is intended so that the mutex won't
     * serialize and blocked by slow operations like UFFDIO_* ioctls.  However
     * this should be enough to make sure the page_requested tree always
     * contains valid information.
     */
    QemuMutex page_request_mutex;
};

MigrationIncomingState *migration_incoming_get_current(void);
void migration_incoming_state_destroy(void);
void migration_incoming_transport_cleanup(MigrationIncomingState *mis);
/*
 * Functions to work with blocktime context
 */
void fill_destination_postcopy_migration_info(MigrationInfo *info);

#define TYPE_MIGRATION "migration"

typedef struct MigrationClass MigrationClass;
DECLARE_OBJ_CHECKERS(MigrationState, MigrationClass,
                     MIGRATION_OBJ, TYPE_MIGRATION)

struct MigrationClass {
    /*< private >*/
    DeviceClass parent_class;
};

struct MigrationState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    QemuThread thread;
    QEMUBH *vm_start_bh;
    QEMUBH *cleanup_bh;
    /* Protected by qemu_file_lock */
    QEMUFile *to_dst_file;
    /* Postcopy specific transfer channel */
    QEMUFile *postcopy_qemufile_src;
    /*
     * It is posted when the preempt channel is established.  Note: this is
     * used for both the start or recover of a postcopy migration.  We'll
     * post to this sem every time a new preempt channel is created in the
     * main thread, and we keep post() and wait() in pair.
     */
    QemuSemaphore postcopy_qemufile_src_sem;
    QIOChannelBuffer *bioc;
    /*
     * Protects to_dst_file/from_dst_file pointers.  We need to make sure we
     * won't yield or hang during the critical section, since this lock will be
     * used in OOB command handler.
     */
    QemuMutex qemu_file_lock;

    /*
     * Used to allow urgent requests to override rate limiting.
     */
    QemuSemaphore rate_limit_sem;

    /* pages already send at the beginning of current iteration */
    uint64_t iteration_initial_pages;

    /* pages transferred per second */
    double pages_per_second;

    /* bytes already send at the beginning of current iteration */
    uint64_t iteration_initial_bytes;
    /* time at the start of current iteration */
    int64_t iteration_start_time;
    /*
     * The final stage happens when the remaining data is smaller than
     * this threshold; it's calculated from the requested downtime and
     * measured bandwidth
     */
    int64_t threshold_size;

    /* params from 'migrate-set-parameters' */
    MigrationParameters parameters;

    int state;

    /* State related to return path */
    struct {
        /* Protected by qemu_file_lock */
        QEMUFile     *from_dst_file;
        QemuThread    rp_thread;
        bool          error;
        /*
         * We can also check non-zero of rp_thread, but there's no "official"
         * way to do this, so this bool makes it slightly more elegant.
         * Checking from_dst_file for this is racy because from_dst_file will
         * be cleared in the rp_thread!
         */
        bool          rp_thread_created;
        QemuSemaphore rp_sem;
    } rp_state;

    double mbps;
    /* Timestamp when recent migration starts (ms) */
    int64_t start_time;
    /* Total time used by latest migration (ms) */
    int64_t total_time;
    /* Timestamp when VM is down (ms) to migrate the last stuff */
    int64_t downtime_start;
    int64_t downtime;
    int64_t expected_downtime;
    bool enabled_capabilities[MIGRATION_CAPABILITY__MAX];
    int64_t setup_time;
    /*
     * Whether guest was running when we enter the completion stage.
     * If migration is interrupted by any reason, we need to continue
     * running the guest on source.
     */
    bool vm_was_running;

    /* Flag set once the migration has been asked to enter postcopy */
    bool start_postcopy;
    /* Flag set after postcopy has sent the device state */
    bool postcopy_after_devices;

    /* Flag set once the migration thread is running (and needs joining) */
    bool migration_thread_running;

    /* Flag set once the migration thread called bdrv_inactivate_all */
    bool block_inactive;

    /* Migration is waiting for guest to unplug device */
    QemuSemaphore wait_unplug_sem;

    /* Migration is paused due to pause-before-switchover */
    QemuSemaphore pause_sem;

    /* The semaphore is used to notify COLO thread that failover is finished */
    QemuSemaphore colo_exit_sem;

    /* The event is used to notify COLO thread to do checkpoint */
    QemuEvent colo_checkpoint_event;
    int64_t colo_checkpoint_time;
    QEMUTimer *colo_delay_timer;

    /* The first error that has occurred.
       We used the mutex to be able to return the 1st error message */
    Error *error;
    /* mutex to protect errp */
    QemuMutex error_mutex;

    /* Do we have to clean up -b/-i from old migrate parameters */
    /* This feature is deprecated and will be removed */
    bool must_remove_block_options;

    /*
     * Global switch on whether we need to store the global state
     * during migration.
     */
    bool store_global_state;

    /* Whether we send QEMU_VM_CONFIGURATION during migration */
    bool send_configuration;
    /* Whether we send section footer during migration */
    bool send_section_footer;

    /* Needed by postcopy-pause state */
    QemuSemaphore postcopy_pause_sem;
    QemuSemaphore postcopy_pause_rp_sem;
    /*
     * Whether we abort the migration if decompression errors are
     * detected at the destination. It is left at false for qemu
     * older than 3.0, since only newer qemu sends streams that
     * do not trigger spurious decompression errors.
     */
    bool decompress_error_check;

    /*
     * This decides the size of guest memory chunk that will be used
     * to track dirty bitmap clearing.  The size of memory chunk will
     * be GUEST_PAGE_SIZE << N.  Say, N=0 means we will clear dirty
     * bitmap for each page to send (1<<0=1); N=10 means we will clear
     * dirty bitmap only once for 1<<10=1K continuous guest pages
     * (which is in 4M chunk).
     */
    uint8_t clear_bitmap_shift;

    /*
     * This save hostname when out-going migration starts
     */
    char *hostname;
};

void migrate_set_state(int *state, int old_state, int new_state);

void migration_fd_process_incoming(QEMUFile *f, Error **errp);
void migration_ioc_process_incoming(QIOChannel *ioc, Error **errp);
void migration_incoming_process(void);

bool  migration_has_all_channels(void);

uint64_t migrate_max_downtime(void);

void migrate_set_error(MigrationState *s, const Error *error);
void migrate_fd_error(MigrationState *s, const Error *error);

void migrate_fd_connect(MigrationState *s, Error *error_in);

bool migration_is_setup_or_active(int state);
bool migration_is_running(int state);

void migrate_init(MigrationState *s);
bool migration_is_blocked(Error **errp);
/* True if outgoing migration has entered postcopy phase */
bool migration_in_postcopy(void);
MigrationState *migrate_get_current(void);

bool migrate_postcopy(void);

bool migrate_release_ram(void);
bool migrate_postcopy_ram(void);
bool migrate_zero_blocks(void);
bool migrate_dirty_bitmaps(void);
bool migrate_ignore_shared(void);
bool migrate_validate_uuid(void);

bool migrate_auto_converge(void);
bool migrate_use_multifd(void);
bool migrate_pause_before_switchover(void);
int migrate_multifd_channels(void);
MultiFDCompression migrate_multifd_compression(void);
int migrate_multifd_zlib_level(void);
int migrate_multifd_zstd_level(void);

#ifdef CONFIG_LINUX
bool migrate_use_zero_copy_send(void);
#else
#define migrate_use_zero_copy_send() (false)
#endif
int migrate_use_tls(void);
int migrate_use_xbzrle(void);
uint64_t migrate_xbzrle_cache_size(void);
bool migrate_colo_enabled(void);

bool migrate_use_block(void);
bool migrate_use_block_incremental(void);
int migrate_max_cpu_throttle(void);
bool migrate_use_return_path(void);

uint64_t ram_get_total_transferred_pages(void);

bool migrate_use_compression(void);
int migrate_compress_level(void);
int migrate_compress_threads(void);
int migrate_compress_wait_thread(void);
int migrate_decompress_threads(void);
bool migrate_use_events(void);
bool migrate_postcopy_blocktime(void);
bool migrate_background_snapshot(void);
bool migrate_postcopy_preempt(void);

/* Sending on the return path - generic and then for each message type */
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value);
int migrate_send_rp_req_pages(MigrationIncomingState *mis, RAMBlock *rb,
                              ram_addr_t start, uint64_t haddr);
int migrate_send_rp_message_req_pages(MigrationIncomingState *mis,
                                      RAMBlock *rb, ram_addr_t start);
void migrate_send_rp_recv_bitmap(MigrationIncomingState *mis,
                                 char *block_name);
void migrate_send_rp_resume_ack(MigrationIncomingState *mis, uint32_t value);

void dirty_bitmap_mig_before_vm_start(void);
void dirty_bitmap_mig_cancel_outgoing(void);
void dirty_bitmap_mig_cancel_incoming(void);
bool check_dirty_bitmap_mig_alias_map(const BitmapMigrationNodeAliasList *bbm,
                                      Error **errp);

void migrate_add_address(SocketAddress *address);

int foreach_not_ignored_block(RAMBlockIterFunc func, void *opaque);

#define qemu_ram_foreach_block \
  #warning "Use foreach_not_ignored_block in migration code"

void migration_make_urgent_request(void);
void migration_consume_urgent_request(void);
bool migration_rate_limit(void);
void migration_cancel(const Error *error);

void populate_vfio_info(MigrationInfo *info);
void postcopy_temp_page_reset(PostcopyTmpPage *tmp_page);

bool migrate_multi_channels_is_allowed(void);
void migrate_protocol_allow_multi_channels(bool allow);

#endif
