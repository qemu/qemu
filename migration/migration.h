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
#include "qobject/json-writer.h"
#include "qemu/thread.h"
#include "qemu/coroutine.h"
#include "io/channel.h"
#include "io/channel-buffer.h"
#include "net/announce.h"
#include "qom/object.h"
#include "postcopy-ram.h"
#include "system/runstate.h"
#include "migration/misc.h"

#define  MIGRATION_THREAD_SNAPSHOT          "mig/snapshot"
#define  MIGRATION_THREAD_DIRTY_RATE        "mig/dirtyrate"

#define  MIGRATION_THREAD_SRC_MAIN          "mig/src/main"
#define  MIGRATION_THREAD_SRC_MULTIFD       "mig/src/send_%d"
#define  MIGRATION_THREAD_SRC_RETURN        "mig/src/return"
#define  MIGRATION_THREAD_SRC_TLS           "mig/src/tls"

#define  MIGRATION_THREAD_DST_COLO          "mig/dst/colo"
#define  MIGRATION_THREAD_DST_MULTIFD       "mig/dst/recv_%d"
#define  MIGRATION_THREAD_DST_FAULT         "mig/dst/fault"
#define  MIGRATION_THREAD_DST_LISTEN        "mig/dst/listen"
#define  MIGRATION_THREAD_DST_PREEMPT       "mig/dst/preempt"

struct PostcopyBlocktimeContext;
typedef struct ThreadPool ThreadPool;

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

typedef enum {
    PREEMPT_THREAD_NONE = 0,
    PREEMPT_THREAD_CREATED,
    PREEMPT_THREAD_QUIT,
} PreemptThreadStatus;

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
    /*
     * When postcopy_qemufile_dst is properly setup, this sem is posted.
     * One can wait on this semaphore to wait until the preempt channel is
     * properly setup.
     */
    QemuSemaphore postcopy_qemufile_dst_done;
    /* Postcopy priority thread is used to receive postcopy requested pages */
    QemuThread postcopy_prio_thread;
    /*
     * Always set by the main vm load thread only, but can be read by the
     * postcopy preempt thread.  "volatile" makes sure all reads will be
     * up-to-date across cores.
     */
    volatile PreemptThreadStatus preempt_thread_status;
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

    MigrationStatus state;

    /*
     * The incoming migration coroutine, non-NULL during qemu_loadvm_state().
     * Used to wake the migration incoming coroutine from rdma code. How much is
     * it safe - it's a question.
     */
    Coroutine *loadvm_co;

    /* The coroutine we should enter (back) after failover */
    Coroutine *colo_incoming_co;
    QemuSemaphore colo_incoming_sem;

    /* Optional load threads pool and its thread exit request flag */
    ThreadPool *load_threads;
    bool load_threads_abort;

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
    /*
     * For postcopy only, count the number of requested page faults that
     * still haven't been resolved.
     */
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
    /*
     * If postcopy preempt is enabled, there is a chance that the main
     * thread finished loading its data before the preempt channel has
     * finished loading the urgent pages.  If that happens, the two threads
     * will use this condvar to synchronize, so the main thread will always
     * wait until all pages received.
     */
    QemuCond page_request_cond;

    /*
     * Number of devices that have yet to approve switchover. When this reaches
     * zero an ACK that it's OK to do switchover is sent to the source. No lock
     * is needed as this field is updated serially.
     */
    unsigned int switchover_ack_pending_num;

    /* Do exit on incoming migration failure */
    bool exit_on_error;
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
     * measured bandwidth, or avail-switchover-bandwidth if specified.
     */
    uint64_t threshold_size;

    /* params from 'migrate-set-parameters' */
    MigrationParameters parameters;

    MigrationStatus state;

    /* State related to return path */
    struct {
        /* Protected by qemu_file_lock */
        QEMUFile     *from_dst_file;
        QemuThread    rp_thread;
        /*
         * We can also check non-zero of rp_thread, but there's no "official"
         * way to do this, so this bool makes it slightly more elegant.
         * Checking from_dst_file for this is racy because from_dst_file will
         * be cleared in the rp_thread!
         */
        bool          rp_thread_created;
        /*
         * Used to synchronize between migration main thread and return
         * path thread.  The migration thread can wait() on this sem, while
         * other threads (e.g., return path thread) can kick it using a
         * post().
         */
        QemuSemaphore rp_sem;
        /*
         * We post to this when we got one PONG from dest. So far it's an
         * easy way to know the main channel has successfully established
         * on dest QEMU.
         */
        QemuSemaphore rp_pong_acks;
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
    bool capabilities[MIGRATION_CAPABILITY__MAX];
    int64_t setup_time;

    /*
     * State before stopping the vm by vm_stop_force_state().
     * If migration is interrupted by any reason, we need to continue
     * running the guest on source if it was running or restore its stopped
     * state.
     */
    RunState vm_old_state;

    /* Flag set once the migration has been asked to enter postcopy */
    bool start_postcopy;

    /* Flag set once the migration thread is running (and needs joining) */
    bool migration_thread_running;

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

    /*
     * Global switch on whether we need to store the global state
     * during migration.
     */
    bool store_global_state;

    /* Whether we send QEMU_VM_CONFIGURATION during migration */
    bool send_configuration;
    /* Whether we send section footer during migration */
    bool send_section_footer;
    /* Whether we send switchover start notification during migration */
    bool send_switchover_start;

    /* Needed by postcopy-pause state */
    QemuSemaphore postcopy_pause_sem;
    /*
     * This variable only affects behavior when postcopy preempt mode is
     * enabled.
     *
     * When set:
     *
     * - postcopy preempt src QEMU instance will generate an EOS message at
     *   the end of migration to shut the preempt channel on dest side.
     *
     * - postcopy preempt channel will be created at the setup phase on src
         QEMU.
     *
     * When clear:
     *
     * - postcopy preempt src QEMU instance will _not_ generate an EOS
     *   message at the end of migration, the dest qemu will shutdown the
     *   channel itself.
     *
     * - postcopy preempt channel will be created at the switching phase
     *   from precopy -> postcopy (to avoid race condition of misordered
     *   creation of channels).
     *
     * NOTE: See message-id <ZBoShWArKDPpX/D7@work-vm> on qemu-devel
     * mailing list for more information on the possible race.  Everyone
     * should probably just keep this value untouched after set by the
     * machine type (or the default).
     */
    bool preempt_pre_7_2;

    /*
     * flush every channel after each section sent.
     *
     * This assures that we can't mix pages from one iteration through
     * ram pages with pages for the following iteration.  We really
     * only need to do this flush after we have go through all the
     * dirty pages.  For historical reasons, we do that after each
     * section.  This is suboptimal (we flush too many times).
     * Default value is false. (since 8.1)
     */
    bool multifd_flush_after_each_section;

    /*
     * This variable only makes sense when set on the machine that is
     * the destination of a multifd migration with TLS enabled. It
     * affects the behavior of the last send->recv iteration with
     * regards to termination of the TLS session.
     *
     * When set:
     *
     * - the destination QEMU instance can expect to never get a
     *   GNUTLS_E_PREMATURE_TERMINATION error. Manifested as the error
     *   message: "The TLS connection was non-properly terminated".
     *
     * When clear:
     *
     * - the destination QEMU instance can expect to see a
     *   GNUTLS_E_PREMATURE_TERMINATION error in any multifd channel
     *   whenever the last recv() call of that channel happens after
     *   the source QEMU instance has already issued shutdown() on the
     *   channel.
     *
     *   Commit 637280aeb2 (since 9.1) introduced a side effect that
     *   causes the destination instance to not be affected by the
     *   premature termination, while commit 1d457daf86 (since 10.0)
     *   causes the premature termination condition to be once again
     *   reachable.
     *
     * NOTE: Regardless of the state of this option, a premature
     * termination of the TLS connection might happen due to error at
     * any moment prior to the last send->recv iteration.
     */
    bool multifd_clean_tls_termination;

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

    /* QEMU_VM_VMDESCRIPTION content filled for all non-iterable devices. */
    JSONWriter *vmdesc;

    /*
     * Indicates whether an ACK from the destination that it's OK to do
     * switchover has been received.
     */
    bool switchover_acked;
    /* Is this a rdma migration */
    bool rdma_migration;

    GSource *hup_source;
};

void migrate_set_state(MigrationStatus *state, MigrationStatus old_state,
                       MigrationStatus new_state);

void migration_fd_process_incoming(QEMUFile *f);
void migration_ioc_process_incoming(QIOChannel *ioc, Error **errp);
void migration_incoming_process(void);

bool  migration_has_all_channels(void);

void migrate_set_error(MigrationState *s, const Error *error);
bool migrate_has_error(MigrationState *s);

void migration_connect(MigrationState *s, Error *error_in);

int migration_call_notifiers(MigrationState *s, MigrationEventType type,
                             Error **errp);

int migrate_init(MigrationState *s, Error **errp);
bool migration_is_blocked(Error **errp);
/* True if outgoing migration has entered postcopy phase */
bool migration_in_postcopy(void);
bool migration_postcopy_is_alive(MigrationStatus state);
MigrationState *migrate_get_current(void);
bool migration_has_failed(MigrationState *);
bool migrate_mode_is_cpr(MigrationState *);

uint64_t ram_get_total_transferred_pages(void);

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
int migrate_send_rp_switchover_ack(MigrationIncomingState *mis);

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
void migration_bh_schedule(QEMUBHFunc *cb, void *opaque);
void migration_cancel(void);

void migration_populate_vfio_info(MigrationInfo *info);
void migration_reset_vfio_bytes_transferred(void);
void postcopy_temp_page_reset(PostcopyTmpPage *tmp_page);

/*
 * Migration thread waiting for return path thread.  Return non-zero if an
 * error is detected.
 */
int migration_rp_wait(MigrationState *s);
/*
 * Kick the migration thread waiting for return path messages.  NOTE: the
 * name can be slightly confusing (when read as "kick the rp thread"), just
 * to remember the target is always the migration thread.
 */
void migration_rp_kick(MigrationState *s);

void migration_bitmap_sync_precopy(bool last_stage);

/* migration/block-dirty-bitmap.c */
void dirty_bitmap_mig_init(void);
bool should_send_vmdesc(void);

#endif
