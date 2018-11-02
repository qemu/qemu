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

#include "qemu-common.h"
#include "qapi/qapi-types-migration.h"
#include "qemu/thread.h"
#include "exec/cpu-common.h"
#include "qemu/coroutine_int.h"
#include "hw/qdev.h"
#include "io/channel.h"

struct PostcopyBlocktimeContext;

#define  MIGRATION_RESUME_ACK_VALUE  (1)

/* State for the incoming migration */
struct MigrationIncomingState {
    QEMUFile *from_src_file;

    /*
     * Free at the start of the main state load, set as the main thread finishes
     * loading state.
     */
    QemuEvent main_thread_load_event;

    size_t         largest_page_size;
    bool           have_fault_thread;
    QemuThread     fault_thread;
    QemuSemaphore  fault_thread_sem;
    /* Set this when we want the fault thread to quit */
    bool           fault_thread_quit;

    bool           have_listen_thread;
    QemuThread     listen_thread;
    QemuSemaphore  listen_thread_sem;

    /* For the kernel to send us notifications */
    int       userfault_fd;
    /* To notify the fault_thread to wake, e.g., when need to quit */
    int       userfault_event_fd;
    QEMUFile *to_src_file;
    QemuMutex rp_mutex;    /* We send replies from multiple threads */
    /* RAMBlock of last request sent to source */
    RAMBlock *last_rb;
    void     *postcopy_tmp_page;
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
    bool postcopy_recover_triggered;
    QemuSemaphore postcopy_pause_sem_dst;
    QemuSemaphore postcopy_pause_sem_fault;
};

MigrationIncomingState *migration_incoming_get_current(void);
void migration_incoming_state_destroy(void);
/*
 * Functions to work with blocktime context
 */
void fill_destination_postcopy_migration_info(MigrationInfo *info);

#define TYPE_MIGRATION "migration"

#define MIGRATION_CLASS(klass) \
    OBJECT_CLASS_CHECK(MigrationClass, (klass), TYPE_MIGRATION)
#define MIGRATION_OBJ(obj) \
    OBJECT_CHECK(MigrationState, (obj), TYPE_MIGRATION)
#define MIGRATION_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MigrationClass, (obj), TYPE_MIGRATION)

typedef struct MigrationClass {
    /*< private >*/
    DeviceClass parent_class;
} MigrationClass;

struct MigrationState
{
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    size_t bytes_xfer;
    size_t xfer_limit;
    QemuThread thread;
    QEMUBH *cleanup_bh;
    QEMUFile *to_dst_file;
    /*
     * Protects to_dst_file pointer.  We need to make sure we won't
     * yield or hang during the critical section, since this lock will
     * be used in OOB command handler.
     */
    QemuMutex qemu_file_lock;

    /*
     * Used to allow urgent requests to override rate limiting.
     */
    QemuSemaphore rate_limit_sem;

    /* bytes already send at the beggining of current interation */
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
        QEMUFile     *from_dst_file;
        QemuThread    rp_thread;
        bool          error;
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

    /* Migration is paused due to pause-before-switchover */
    QemuSemaphore pause_sem;

    /* The semaphore is used to notify COLO thread that failover is finished */
    QemuSemaphore colo_exit_sem;

    /* The semaphore is used to notify COLO thread to do checkpoint */
    QemuSemaphore colo_checkpoint_sem;
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

    /* Whether the VM is only allowing for migratable devices */
    bool only_migratable;

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
};

void migrate_set_state(int *state, int old_state, int new_state);

void migration_fd_process_incoming(QEMUFile *f);
void migration_ioc_process_incoming(QIOChannel *ioc);
void migration_incoming_process(void);

bool  migration_has_all_channels(void);

uint64_t migrate_max_downtime(void);

void migrate_set_error(MigrationState *s, const Error *error);
void migrate_fd_error(MigrationState *s, const Error *error);

void migrate_fd_connect(MigrationState *s, Error *error_in);

bool migration_is_setup_or_active(int state);

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

bool migrate_auto_converge(void);
bool migrate_use_multifd(void);
bool migrate_pause_before_switchover(void);
int migrate_multifd_channels(void);
int migrate_multifd_page_count(void);

int migrate_use_xbzrle(void);
int64_t migrate_xbzrle_cache_size(void);
bool migrate_colo_enabled(void);

bool migrate_use_block(void);
bool migrate_use_block_incremental(void);
int migrate_max_cpu_throttle(void);
bool migrate_use_return_path(void);

bool migrate_use_compression(void);
int migrate_compress_level(void);
int migrate_compress_threads(void);
int migrate_compress_wait_thread(void);
int migrate_decompress_threads(void);
bool migrate_use_events(void);
bool migrate_postcopy_blocktime(void);

/* Sending on the return path - generic and then for each message type */
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value);
int migrate_send_rp_req_pages(MigrationIncomingState *mis, const char* rbname,
                              ram_addr_t start, size_t len);
void migrate_send_rp_recv_bitmap(MigrationIncomingState *mis,
                                 char *block_name);
void migrate_send_rp_resume_ack(MigrationIncomingState *mis, uint32_t value);

void dirty_bitmap_mig_before_vm_start(void);
void init_dirty_bitmap_incoming_migration(void);

#define qemu_ram_foreach_block \
  #warning "Use qemu_ram_foreach_block_migratable in migration code"

void migration_make_urgent_request(void);
void migration_consume_urgent_request(void);

#endif
