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
#include "qemu/thread.h"
#include "qapi-types.h"
#include "exec/cpu-common.h"
#include "qemu/coroutine_int.h"
#include "hw/qdev.h"

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

    bool           have_listen_thread;
    QemuThread     listen_thread;
    QemuSemaphore  listen_thread_sem;

    /* For the kernel to send us notifications */
    int       userfault_fd;
    /* To tell the fault_thread to quit */
    int       userfault_quit_fd;
    QEMUFile *to_src_file;
    QemuMutex rp_mutex;    /* We send replies from multiple threads */
    void     *postcopy_tmp_page;
    void     *postcopy_tmp_zero_page;

    QEMUBH *bh;

    int state;

    bool have_colo_incoming_thread;
    QemuThread colo_incoming_thread;
    /* The coroutine we should enter (back) after failover */
    Coroutine *migration_incoming_co;
    QemuSemaphore colo_incoming_sem;
};

MigrationIncomingState *migration_incoming_get_current(void);
void migration_incoming_state_destroy(void);

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

    /* params from 'migrate-set-parameters' */
    MigrationParameters parameters;

    int state;

    /* State related to return path */
    struct {
        QEMUFile     *from_dst_file;
        QemuThread    rp_thread;
        bool          error;
    } rp_state;

    double mbps;
    int64_t total_time;
    int64_t downtime;
    int64_t expected_downtime;
    bool enabled_capabilities[MIGRATION_CAPABILITY__MAX];
    int64_t xbzrle_cache_size;
    int64_t setup_time;

    /* Flag set once the migration has been asked to enter postcopy */
    bool start_postcopy;
    /* Flag set after postcopy has sent the device state */
    bool postcopy_after_devices;

    /* Flag set once the migration thread is running (and needs joining) */
    bool migration_thread_running;

    /* Flag set once the migration thread called bdrv_inactivate_all */
    bool block_inactive;

    /* The semaphore is used to notify COLO thread that failover is finished */
    QemuSemaphore colo_exit_sem;

    /* The semaphore is used to notify COLO thread to do checkpoint */
    QemuSemaphore colo_checkpoint_sem;
    int64_t colo_checkpoint_time;
    QEMUTimer *colo_delay_timer;

    /* The last error that occurred */
    Error *error;
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
};

void migrate_set_state(int *state, int old_state, int new_state);

void migration_fd_process_incoming(QEMUFile *f);

uint64_t migrate_max_downtime(void);

void migrate_fd_error(MigrationState *s, const Error *error);

void migrate_fd_connect(MigrationState *s);

MigrationState *migrate_init(void);
bool migration_is_blocked(Error **errp);
/* True if outgoing migration has entered postcopy phase */
bool migration_in_postcopy(void);
MigrationState *migrate_get_current(void);

bool migrate_release_ram(void);
bool migrate_postcopy_ram(void);
bool migrate_zero_blocks(void);

bool migrate_auto_converge(void);

int migrate_use_xbzrle(void);
int64_t migrate_xbzrle_cache_size(void);
bool migrate_colo_enabled(void);

bool migrate_use_block(void);
bool migrate_use_block_incremental(void);
bool migrate_use_return_path(void);

bool migrate_use_compression(void);
int migrate_compress_level(void);
int migrate_compress_threads(void);
int migrate_decompress_threads(void);
bool migrate_use_events(void);

/* Sending on the return path - generic and then for each message type */
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_req_pages(MigrationIncomingState *mis, const char* rbname,
                              ram_addr_t start, size_t len);

#endif
