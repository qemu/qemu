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

#ifndef QEMU_MIGRATION_OPTIONS_H
#define QEMU_MIGRATION_OPTIONS_H

/* capabilities */

bool migrate_auto_converge(void);
bool migrate_background_snapshot(void);
bool migrate_block(void);
bool migrate_colo(void);
bool migrate_compress(void);
bool migrate_dirty_bitmaps(void);
bool migrate_events(void);
bool migrate_ignore_shared(void);
bool migrate_late_block_activate(void);
bool migrate_multifd(void);
bool migrate_pause_before_switchover(void);
bool migrate_postcopy_blocktime(void);
bool migrate_postcopy_preempt(void);
bool migrate_postcopy_ram(void);
bool migrate_rdma_pin_all(void);
bool migrate_release_ram(void);
bool migrate_return_path(void);
bool migrate_validate_uuid(void);
bool migrate_xbzrle(void);
bool migrate_zero_blocks(void);
bool migrate_zero_copy_send(void);

/* capabilities helpers */

bool migrate_caps_check(bool *old_caps, bool *new_caps, Error **errp);
bool migrate_cap_set(int cap, bool value, Error **errp);

/* parameters */

bool migrate_block_incremental(void);
int migrate_compress_level(void);
int migrate_compress_threads(void);
int migrate_compress_wait_thread(void);
int migrate_decompress_threads(void);
int64_t migrate_max_postcopy_bandwidth(void);
int migrate_multifd_channels(void);
MultiFDCompression migrate_multifd_compression(void);
int migrate_multifd_zlib_level(void);
int migrate_multifd_zstd_level(void);
uint8_t migrate_throttle_trigger_threshold(void);
uint64_t migrate_xbzrle_cache_size(void);

#endif
