/*
 * QEMU migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *   Orit Wasserman <owasserm@redhat.com>
 *   Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/runstate.h"
#include "migration.h"
#include "ram.h"
#include "options.h"

bool migrate_auto_converge(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_AUTO_CONVERGE];
}

bool migrate_background_snapshot(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT];
}

bool migrate_block(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_BLOCK];
}

bool migrate_colo(void)
{
    MigrationState *s = migrate_get_current();
    return s->capabilities[MIGRATION_CAPABILITY_X_COLO];
}

bool migrate_compress(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_COMPRESS];
}

bool migrate_dirty_bitmaps(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_DIRTY_BITMAPS];
}

bool migrate_events(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_EVENTS];
}

bool migrate_ignore_shared(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_X_IGNORE_SHARED];
}

bool migrate_late_block_activate(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_LATE_BLOCK_ACTIVATE];
}

bool migrate_multifd(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_MULTIFD];
}

bool migrate_pause_before_switchover(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_PAUSE_BEFORE_SWITCHOVER];
}

bool migrate_postcopy_blocktime(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_POSTCOPY_BLOCKTIME];
}

bool migrate_postcopy_preempt(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_POSTCOPY_PREEMPT];
}

bool migrate_postcopy_ram(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_POSTCOPY_RAM];
}

bool migrate_rdma_pin_all(void)
{
    MigrationState *s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_RDMA_PIN_ALL];
}

bool migrate_release_ram(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_RELEASE_RAM];
}

bool migrate_return_path(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_RETURN_PATH];
}

bool migrate_validate_uuid(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_VALIDATE_UUID];
}

bool migrate_xbzrle(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_XBZRLE];
}

bool migrate_zero_blocks(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_ZERO_BLOCKS];
}

bool migrate_zero_copy_send(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_ZERO_COPY_SEND];
}
typedef enum WriteTrackingSupport {
    WT_SUPPORT_UNKNOWN = 0,
    WT_SUPPORT_ABSENT,
    WT_SUPPORT_AVAILABLE,
    WT_SUPPORT_COMPATIBLE
} WriteTrackingSupport;

static
WriteTrackingSupport migrate_query_write_tracking(void)
{
    /* Check if kernel supports required UFFD features */
    if (!ram_write_tracking_available()) {
        return WT_SUPPORT_ABSENT;
    }
    /*
     * Check if current memory configuration is
     * compatible with required UFFD features.
     */
    if (!ram_write_tracking_compatible()) {
        return WT_SUPPORT_AVAILABLE;
    }

    return WT_SUPPORT_COMPATIBLE;
}

/* Migration capabilities set */
struct MigrateCapsSet {
    int size;                       /* Capability set size */
    MigrationCapability caps[];     /* Variadic array of capabilities */
};
typedef struct MigrateCapsSet MigrateCapsSet;

/* Define and initialize MigrateCapsSet */
#define INITIALIZE_MIGRATE_CAPS_SET(_name, ...)   \
    MigrateCapsSet _name = {    \
        .size = sizeof((int []) { __VA_ARGS__ }) / sizeof(int), \
        .caps = { __VA_ARGS__ } \
    }

/* Background-snapshot compatibility check list */
static const
INITIALIZE_MIGRATE_CAPS_SET(check_caps_background_snapshot,
    MIGRATION_CAPABILITY_POSTCOPY_RAM,
    MIGRATION_CAPABILITY_DIRTY_BITMAPS,
    MIGRATION_CAPABILITY_POSTCOPY_BLOCKTIME,
    MIGRATION_CAPABILITY_LATE_BLOCK_ACTIVATE,
    MIGRATION_CAPABILITY_RETURN_PATH,
    MIGRATION_CAPABILITY_MULTIFD,
    MIGRATION_CAPABILITY_PAUSE_BEFORE_SWITCHOVER,
    MIGRATION_CAPABILITY_AUTO_CONVERGE,
    MIGRATION_CAPABILITY_RELEASE_RAM,
    MIGRATION_CAPABILITY_RDMA_PIN_ALL,
    MIGRATION_CAPABILITY_COMPRESS,
    MIGRATION_CAPABILITY_XBZRLE,
    MIGRATION_CAPABILITY_X_COLO,
    MIGRATION_CAPABILITY_VALIDATE_UUID,
    MIGRATION_CAPABILITY_ZERO_COPY_SEND);

/**
 * @migration_caps_check - check capability compatibility
 *
 * @old_caps: old capability list
 * @new_caps: new capability list
 * @errp: set *errp if the check failed, with reason
 *
 * Returns true if check passed, otherwise false.
 */
bool migrate_caps_check(bool *old_caps, bool *new_caps, Error **errp)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

#ifndef CONFIG_LIVE_BLOCK_MIGRATION
    if (new_caps[MIGRATION_CAPABILITY_BLOCK]) {
        error_setg(errp, "QEMU compiled without old-style (blk/-b, inc/-i) "
                   "block migration");
        error_append_hint(errp, "Use drive_mirror+NBD instead.\n");
        return false;
    }
#endif

#ifndef CONFIG_REPLICATION
    if (new_caps[MIGRATION_CAPABILITY_X_COLO]) {
        error_setg(errp, "QEMU compiled without replication module"
                   " can't enable COLO");
        error_append_hint(errp, "Please enable replication before COLO.\n");
        return false;
    }
#endif

    if (new_caps[MIGRATION_CAPABILITY_POSTCOPY_RAM]) {
        /* This check is reasonably expensive, so only when it's being
         * set the first time, also it's only the destination that needs
         * special support.
         */
        if (!old_caps[MIGRATION_CAPABILITY_POSTCOPY_RAM] &&
            runstate_check(RUN_STATE_INMIGRATE) &&
            !postcopy_ram_supported_by_host(mis)) {
            /* postcopy_ram_supported_by_host will have emitted a more
             * detailed message
             */
            error_setg(errp, "Postcopy is not supported");
            return false;
        }

        if (new_caps[MIGRATION_CAPABILITY_X_IGNORE_SHARED]) {
            error_setg(errp, "Postcopy is not compatible with ignore-shared");
            return false;
        }
    }

    if (new_caps[MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT]) {
        WriteTrackingSupport wt_support;
        int idx;
        /*
         * Check if 'background-snapshot' capability is supported by
         * host kernel and compatible with guest memory configuration.
         */
        wt_support = migrate_query_write_tracking();
        if (wt_support < WT_SUPPORT_AVAILABLE) {
            error_setg(errp, "Background-snapshot is not supported by host kernel");
            return false;
        }
        if (wt_support < WT_SUPPORT_COMPATIBLE) {
            error_setg(errp, "Background-snapshot is not compatible "
                    "with guest memory configuration");
            return false;
        }

        /*
         * Check if there are any migration capabilities
         * incompatible with 'background-snapshot'.
         */
        for (idx = 0; idx < check_caps_background_snapshot.size; idx++) {
            int incomp_cap = check_caps_background_snapshot.caps[idx];
            if (new_caps[incomp_cap]) {
                error_setg(errp,
                        "Background-snapshot is not compatible with %s",
                        MigrationCapability_str(incomp_cap));
                return false;
            }
        }
    }

#ifdef CONFIG_LINUX
    if (new_caps[MIGRATION_CAPABILITY_ZERO_COPY_SEND] &&
        (!new_caps[MIGRATION_CAPABILITY_MULTIFD] ||
         new_caps[MIGRATION_CAPABILITY_COMPRESS] ||
         new_caps[MIGRATION_CAPABILITY_XBZRLE] ||
         migrate_multifd_compression() ||
         migrate_use_tls())) {
        error_setg(errp,
                   "Zero copy only available for non-compressed non-TLS multifd migration");
        return false;
    }
#else
    if (new_caps[MIGRATION_CAPABILITY_ZERO_COPY_SEND]) {
        error_setg(errp,
                   "Zero copy currently only available on Linux");
        return false;
    }
#endif

    if (new_caps[MIGRATION_CAPABILITY_POSTCOPY_PREEMPT]) {
        if (!new_caps[MIGRATION_CAPABILITY_POSTCOPY_RAM]) {
            error_setg(errp, "Postcopy preempt requires postcopy-ram");
            return false;
        }

        /*
         * Preempt mode requires urgent pages to be sent in separate
         * channel, OTOH compression logic will disorder all pages into
         * different compression channels, which is not compatible with the
         * preempt assumptions on channel assignments.
         */
        if (new_caps[MIGRATION_CAPABILITY_COMPRESS]) {
            error_setg(errp, "Postcopy preempt not compatible with compress");
            return false;
        }
    }

    if (new_caps[MIGRATION_CAPABILITY_MULTIFD]) {
        if (new_caps[MIGRATION_CAPABILITY_COMPRESS]) {
            error_setg(errp, "Multifd is not compatible with compress");
            return false;
        }
    }

    return true;
}

bool migrate_cap_set(int cap, bool value, Error **errp)
{
    MigrationState *s = migrate_get_current();
    bool new_caps[MIGRATION_CAPABILITY__MAX];

    if (migration_is_running(s->state)) {
        error_setg(errp, QERR_MIGRATION_ACTIVE);
        return false;
    }

    memcpy(new_caps, s->capabilities, sizeof(new_caps));
    new_caps[cap] = value;

    if (!migrate_caps_check(s->capabilities, new_caps, errp)) {
        return false;
    }
    s->capabilities[cap] = value;
    return true;
}

MigrationCapabilityStatusList *qmp_query_migrate_capabilities(Error **errp)
{
    MigrationCapabilityStatusList *head = NULL, **tail = &head;
    MigrationCapabilityStatus *caps;
    MigrationState *s = migrate_get_current();
    int i;

    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
#ifndef CONFIG_LIVE_BLOCK_MIGRATION
        if (i == MIGRATION_CAPABILITY_BLOCK) {
            continue;
        }
#endif
        caps = g_malloc0(sizeof(*caps));
        caps->capability = i;
        caps->state = s->capabilities[i];
        QAPI_LIST_APPEND(tail, caps);
    }

    return head;
}

void qmp_migrate_set_capabilities(MigrationCapabilityStatusList *params,
                                  Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationCapabilityStatusList *cap;
    bool new_caps[MIGRATION_CAPABILITY__MAX];

    if (migration_is_running(s->state)) {
        error_setg(errp, QERR_MIGRATION_ACTIVE);
        return;
    }

    memcpy(new_caps, s->capabilities, sizeof(new_caps));
    for (cap = params; cap; cap = cap->next) {
        new_caps[cap->value->capability] = cap->value->state;
    }

    if (!migrate_caps_check(s->capabilities, new_caps, errp)) {
        return;
    }

    for (cap = params; cap; cap = cap->next) {
        s->capabilities[cap->value->capability] = cap->value->state;
    }
}

/* parameters */

bool migrate_block_incremental(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.block_incremental;
}

uint32_t migrate_checkpoint_delay(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.x_checkpoint_delay;
}

int migrate_compress_level(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.compress_level;
}

int migrate_compress_threads(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.compress_threads;
}

int migrate_compress_wait_thread(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.compress_wait_thread;
}

int migrate_decompress_threads(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.decompress_threads;
}

int64_t migrate_max_postcopy_bandwidth(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.max_postcopy_bandwidth;
}

int migrate_multifd_channels(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.multifd_channels;
}

MultiFDCompression migrate_multifd_compression(void)
{
    MigrationState *s;

    s = migrate_get_current();

    assert(s->parameters.multifd_compression < MULTIFD_COMPRESSION__MAX);
    return s->parameters.multifd_compression;
}

int migrate_multifd_zlib_level(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.multifd_zlib_level;
}

int migrate_multifd_zstd_level(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.multifd_zstd_level;
}

uint8_t migrate_throttle_trigger_threshold(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.throttle_trigger_threshold;
}

uint64_t migrate_xbzrle_cache_size(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters.xbzrle_cache_size;
}
