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

#include "qapi/qmp/qdict.h"
#include "qemu-common.h"
#include "qemu/thread.h"
#include "qemu/notify.h"
#include "qapi-types.h"
#include "exec/cpu-common.h"
#include "qemu/coroutine_int.h"

/* Messages sent on the return path from destination to source */
enum mig_rp_message_type {
    MIG_RP_MSG_INVALID = 0,  /* Must be 0 */
    MIG_RP_MSG_SHUT,         /* sibling will not send any more RP messages */
    MIG_RP_MSG_PONG,         /* Response to a PING; data (seq: be32 ) */

    MIG_RP_MSG_REQ_PAGES_ID, /* data (start: be64, len: be32, id: string) */
    MIG_RP_MSG_REQ_PAGES,    /* data (start: be64, len: be32) */

    MIG_RP_MSG_MAX
};

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

struct MigrationState
{
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
};

void migrate_set_state(int *state, int old_state, int new_state);

void migration_fd_process_incoming(QEMUFile *f);

void qemu_start_incoming_migration(const char *uri, Error **errp);

uint64_t migrate_max_downtime(void);

void migrate_fd_error(MigrationState *s, const Error *error);

void migrate_fd_connect(MigrationState *s);

void add_migration_state_change_notifier(Notifier *notify);
void remove_migration_state_change_notifier(Notifier *notify);
MigrationState *migrate_init(void);
bool migration_is_blocked(Error **errp);
bool migration_in_setup(MigrationState *);
bool migration_is_idle(void);
bool migration_has_finished(MigrationState *);
bool migration_has_failed(MigrationState *);
/* True if outgoing migration has entered postcopy phase */
bool migration_in_postcopy(void);
/* ...and after the device transmission */
bool migration_in_postcopy_after_devices(MigrationState *);
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

bool migrate_use_compression(void);
int migrate_compress_level(void);
int migrate_compress_threads(void);
int migrate_decompress_threads(void);
bool migrate_use_events(void);

/* Sending on the return path - generic and then for each message type */
void migrate_send_rp_message(MigrationIncomingState *mis,
                             enum mig_rp_message_type message_type,
                             uint16_t len, void *data);
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value);
void migrate_send_rp_req_pages(MigrationIncomingState *mis, const char* rbname,
                              ram_addr_t start, size_t len);

void ram_control_before_iterate(QEMUFile *f, uint64_t flags);
void ram_control_after_iterate(QEMUFile *f, uint64_t flags);
void ram_control_load_hook(QEMUFile *f, uint64_t flags, void *data);

/* Whenever this is found in the data stream, the flags
 * will be passed to ram_control_load_hook in the incoming-migration
 * side. This lets before_ram_iterate/after_ram_iterate add
 * transport-specific sections to the RAM migration data.
 */
#define RAM_SAVE_FLAG_HOOK     0x80

#define RAM_SAVE_CONTROL_NOT_SUPP -1000
#define RAM_SAVE_CONTROL_DELAYED  -2000

size_t ram_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                             ram_addr_t offset, size_t size,
                             uint64_t *bytes_sent);

void savevm_skip_section_footers(void);
void register_global_state(void);
void global_state_set_optional(void);
void savevm_skip_configuration(void);
int global_state_store(void);
void global_state_store_running(void);

#endif
